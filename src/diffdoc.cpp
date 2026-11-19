#include "diffdoc.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QMutex>
#include <QRegularExpression>
#include <algorithm>

namespace KS = KSyntaxHighlighting;

namespace {

constexpr int kHighlightBudget = 20000; // lines; bigger diffs are shown plain

class LineHighlighter : public KS::AbstractHighlighter
{
public:
    std::vector<ColorSpan> spans;
    KS::State run(const QString &line, const KS::State &st)
    {
        spans.clear();
        return highlightLine(line, st);
    }

protected:
    void applyFormat(int offset, int length, const KS::Format &f) override
    {
        if (length <= 0 || f.isDefaultTextStyle(theme())) return;
        spans.push_back({offset, length, f.hasTextColor(theme()) ? f.textColor(theme()).rgb() : 0, f.isBold(theme()), f.isItalic(theme())});
    }
};

KS::Repository &syntaxRepo()
{
    static KS::Repository repo; // loaded once, on first diff
    return repo;
}

// Token-level LCS between two lines; changed character ranges on each side.
// Returns false when the lines are too different for word marks to help.
bool wordRanges(const QString &a, const QString &b, Ranges &ra, Ranges &rb)
{
    static const QRegularExpression tok(QStringLiteral("\\w+|\\s+|[^\\w\\s]"));
    auto split = [](const QString &s) {
        std::vector<QStringView> out;
        auto it = tok.globalMatchView(s);
        while (it.hasNext()) out.push_back(it.next().capturedView());
        return out;
    };
    const auto ta = split(a), tb = split(b);
    const size_t n = ta.size(), m = tb.size();
    if (n == 0 || m == 0 || n * m > 90000) return false;
    std::vector<uint16_t> dp((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> uint16_t & { return dp[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = ta[i] == tb[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));

    auto push = [](Ranges &r, int s, int e) {
        if (!r.empty() && r.back().second == s) r.back().second = e;
        else r.push_back({s, e});
    };
    size_t i = 0, j = 0;
    int pa = 0, pb = 0, common = 0;
    while (i < n && j < m) {
        if (ta[i] == tb[j]) {
            if (!ta[i].trimmed().isEmpty()) common += int(ta[i].size());
            pa += int(ta[i].size());
            pb += int(tb[j].size());
            ++i;
            ++j;
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            push(ra, pa, pa + int(ta[i].size()));
            pa += int(ta[i++].size());
        } else {
            push(rb, pb, pb + int(tb[j].size()));
            pb += int(tb[j++].size());
        }
    }
    for (; i < n; ++i) push(ra, pa, pa + int(ta[i].size())), pa += int(ta[i].size());
    for (; j < m; ++j) push(rb, pb, pb + int(tb[j].size())), pb += int(tb[j].size());

    auto joinGaps = [](Ranges &r, const QString &s) {
        Ranges out;
        for (const auto &x : r) {
            if (!out.empty() && s.mid(out.back().second, x.first - out.back().second).trimmed().isEmpty()) out.back().second = x.second;
            else out.push_back(x);
        }
        r.swap(out);
    };
    joinGaps(ra, a);
    joinGaps(rb, b);
    const int longest = int(std::max(a.trimmed().size(), b.trimmed().size()));
    return longest > 0 && common * 10 >= longest * 3;
}

} // namespace

DiffDoc buildDiffDoc(DiffData &&d)
{
    DiffDoc doc;
    int total = 0;
    for (const auto &h : d.hunks) total += int(h.lines.size());
    const bool rich = total <= kHighlightBudget;

    static QMutex hlMutex; // the syntax repository is not thread-safe
    QMutexLocker lock(&hlMutex);
    LineHighlighter hl;
    const KS::Definition def = rich ? syntaxRepo().definitionForFileName(d.path) : KS::Definition();
    const bool useHl = rich && def.isValid();
    if (useHl) {
        hl.setDefinition(def);
        KS::Theme th = syntaxRepo().theme(QStringLiteral("Breeze Dark"));
        hl.setTheme(th.isValid() ? th : syntaxRepo().defaultTheme(KS::Repository::DarkTheme));
    }

    for (auto &h : d.hunks) {
        DocHunk out;
        out.header = h.header;
        const size_t n = h.lines.size();
        out.lines.resize(n);
        KS::State oldSt, newSt;
        for (size_t i = 0; i < n; ++i) {
            DLine &l = h.lines[i];
            DocLine &o = out.lines[i];
            o.t = l.t;
            o.o = l.o;
            o.n = l.n;
            o.text = std::move(l.s);
            if (o.t == '+') ++doc.additions;
            if (o.t == '-') ++doc.deletions;
            doc.maxCols = std::max(doc.maxCols, int(o.text.size()));
            if (useHl) {
                if (o.t == '-') {
                    oldSt = hl.run(o.text, oldSt);
                } else {
                    newSt = hl.run(o.text, newSt);
                    if (o.t == ' ') oldSt = newSt;
                }
                o.spans = hl.spans;
            }
        }
        if (rich) {
            size_t i = 0;
            while (i < n) {
                if (out.lines[i].t != '-') { ++i; continue; }
                size_t j = i;
                while (j < n && out.lines[j].t == '-') ++j;
                size_t k = j;
                while (k < n && out.lines[k].t == '+') ++k;
                for (size_t p = 0; p < std::min(j - i, k - j); ++p) {
                    Ranges ra, rb;
                    if (wordRanges(out.lines[i + p].text, out.lines[j + p].text, ra, rb)) {
                        out.lines[i + p].marks = std::move(ra);
                        out.lines[j + p].marks = std::move(rb);
                    }
                }
                i = std::max(k, i + 1);
            }
        }
        doc.hunks.push_back(std::move(out));
    }
    d.hunks.clear();
    doc.meta = std::move(d);
    return doc;
}
