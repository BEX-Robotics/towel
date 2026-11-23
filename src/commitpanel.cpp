#include "commitpanel.h"
#include "panels.h"
#include "theme.h"

#include <QCheckBox>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel *label(QWidget *parent, int px, bool bold, const QColor &c)
{
    auto *l = new QLabel(parent);
    l->setFont(theme::uiFont(px, bold));
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, c);
    l->setPalette(p);
    return l;
}

QStringList pathsOf(const QVariantMap &f)
{
    QStringList out{f.value("path").toString()};
    if (!f.value("oldPath").toString().isEmpty()) out << f.value("oldPath").toString();
    return out;
}

} // namespace

CommitPanel::CommitPanel(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::panel);
    setPalette(pal);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 10, 0, 12);
    v->setSpacing(4);

    auto section = [&](QLabel *&head, QPushButton *&all, const QString &allText, FileList *&list, const QString &glyph, const QString &tip) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(16, 0, 10, 0);
        head = label(this, 12, true, theme::dim);
        all = new QPushButton(allText, this);
        all->setProperty("chip", true);
        all->setCursor(Qt::PointingHandCursor);
        all->setFocusPolicy(Qt::NoFocus);
        row->addWidget(head);
        row->addStretch();
        row->addWidget(all);
        v->addLayout(row);
        list = new FileList(this);
        list->setAction(glyph, tip);
        auto *scroll = new QScrollArea(this);
        scroll->setWidget(list);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        QPalette sp = scroll->viewport()->palette();
        sp.setColor(QPalette::Window, theme::panel);
        scroll->viewport()->setPalette(sp);
        scroll->viewport()->setAutoFillBackground(true);
        v->addWidget(scroll, 1);
    };
    section(m_unstagedHead, m_stageAll, QStringLiteral("Stage all"), m_unstaged, QStringLiteral("+"), QStringLiteral("Stage (double-click)"));
    v->addSpacing(6);
    section(m_stagedHead, m_unstageAll, QStringLiteral("Unstage all"), m_staged, QStringLiteral("−"), QStringLiteral("Unstage (double-click)"));

    m_conflicts = label(this, 12, false, theme::red);
    m_conflicts->setWordWrap(true);
    m_conflicts->setContentsMargins(16, 4, 16, 4);
    m_conflicts->hide();
    v->addWidget(m_conflicts);

    // message editor
    auto *editor = new QWidget(this);
    auto *ev = new QVBoxLayout(editor);
    ev->setContentsMargins(16, 10, 16, 0);
    ev->setSpacing(6);
    auto *sumRow = new QHBoxLayout;
    m_summary = new QLineEdit(editor);
    m_summary->setPlaceholderText(QStringLiteral("Summary"));
    m_summary->setFont(theme::uiFont(13, true));
    m_counter = label(editor, 11, false, theme::faint);
    m_counter->setMinimumWidth(theme::z(26));
    m_counter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sumRow->addWidget(m_summary, 1);
    sumRow->addWidget(m_counter);
    ev->addLayout(sumRow);
    m_body = new QPlainTextEdit(editor);
    m_body->setPlaceholderText(QStringLiteral("Description (optional)"));
    m_body->setFont(theme::uiFont(13));
    m_body->setFixedHeight(theme::z(110));
    m_body->setTabChangesFocus(true);
    ev->addWidget(m_body);
    m_amendWarn = label(editor, 11, false, theme::yellow);
    m_amendWarn->setWordWrap(true);
    m_amendWarn->setText(QStringLiteral("HEAD is already pushed: amending rewrites it, so the next push must be a force push."));
    m_amendWarn->hide();
    ev->addWidget(m_amendWarn);
    auto *btnRow = new QHBoxLayout;
    m_amend = new QCheckBox(QStringLiteral("Amend last commit"), editor);
    m_amend->setFocusPolicy(Qt::NoFocus);
    m_commit = new QPushButton(editor);
    m_commit->setProperty("primary", true);
    m_commit->setCursor(Qt::PointingHandCursor);
    m_commit->setToolTip(QStringLiteral("Ctrl+Enter"));
    m_stash = new QPushButton(QStringLiteral("Stash"), editor);
    m_stash->setToolTip(QStringLiteral("Stash all changes, including untracked files"));
    m_stash->setCursor(Qt::PointingHandCursor);
    m_stash->setFocusPolicy(Qt::NoFocus);
    connect(m_stash, &QPushButton::clicked, this, &CommitPanel::stashRequested);
    btnRow->addWidget(m_amend);
    btnRow->addStretch();
    btnRow->addWidget(m_stash);
    btnRow->addWidget(m_commit);
    ev->addLayout(btnRow);
    v->addWidget(editor);

    m_draftTimer = new QTimer(this);
    m_draftTimer->setSingleShot(true);
    m_draftTimer->setInterval(400);
    connect(m_draftTimer, &QTimer::timeout, this, &CommitPanel::saveDraft);

    connect(m_unstaged, &FileList::fileClicked, this, [this](const QVariantMap &f) { emit openFile(f, QStringLiteral("unstaged")); });
    connect(m_staged, &FileList::fileClicked, this, [this](const QVariantMap &f) { emit openFile(f, QStringLiteral("staged")); });
    connect(m_unstaged, &FileList::actionClicked, this, [this](const QVariantMap &f) { emit stage(pathsOf(f)); });
    connect(m_staged, &FileList::actionClicked, this, [this](const QVariantMap &f) { emit unstage(pathsOf(f)); });
    connect(m_stageAll, &QPushButton::clicked, this, &CommitPanel::stageAll);
    connect(m_unstageAll, &QPushButton::clicked, this, &CommitPanel::unstageAll);
    connect(m_summary, &QLineEdit::textChanged, this, [this] {
        updateButton();
        m_draftTimer->start();
    });
    connect(m_summary, &QLineEdit::returnPressed, m_body, qOverload<>(&QWidget::setFocus));
    connect(m_body, &QPlainTextEdit::textChanged, m_draftTimer, qOverload<>(&QTimer::start));
    connect(m_amend, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) m_amendWarn->hide();
        emit amendToggled(on);
        updateButton();
    });
    connect(m_commit, &QPushButton::clicked, this, &CommitPanel::triggerCommit);
    updateButton();
}

void CommitPanel::setStatus(const QVariantMap &status)
{
    m_unstagedFiles = status.value("unstaged").toList();
    m_stagedFiles = status.value("staged").toList();
    m_unstaged->setFiles(m_unstagedFiles);
    m_staged->setFiles(m_stagedFiles);
    m_unstagedHead->setText(QStringLiteral("Unstaged  %1").arg(m_unstagedFiles.size()));
    m_stagedHead->setText(QStringLiteral("Staged  %1").arg(m_stagedFiles.size()));
    m_stageAll->setEnabled(!m_unstagedFiles.isEmpty());
    m_unstageAll->setEnabled(!m_stagedFiles.isEmpty());
    m_stash->setEnabled(!m_stagedFiles.isEmpty() || !m_unstagedFiles.isEmpty());
    const QVariantList conflicted = status.value("conflicted").toList();
    QStringList names;
    for (const QVariant &c : conflicted) names << c.toMap().value("path").toString();
    m_conflicts->setVisible(!names.isEmpty());
    m_conflicts->setText(QStringLiteral("Conflicts (resolve in your editor, then stage): ") + names.join(QStringLiteral(", ")));
    updateButton();
}

const QVariantList &CommitPanel::files(const QString &mode) const
{
    return mode == QLatin1String("staged") ? m_stagedFiles : m_unstagedFiles;
}

void CommitPanel::setBranch(const QString &branch)
{
    m_branch = branch;
    updateButton();
}

void CommitPanel::setOpenFile(const QString &mode, const QString &path)
{
    m_unstaged->setOpenPath(mode == QLatin1String("unstaged") ? path : QString());
    m_staged->setOpenPath(mode == QLatin1String("staged") ? path : QString());
}

void CommitPanel::setAmendInfo(const QString &headMessage, bool headPushed)
{
    m_amendWarn->setVisible(m_amend->isChecked() && headPushed);
    if (m_summary->text().isEmpty() && m_body->toPlainText().isEmpty() && !headMessage.isEmpty()) {
        const int nl = headMessage.indexOf(QLatin1Char('\n'));
        m_summary->setText(nl < 0 ? headMessage : headMessage.left(nl));
        m_body->setPlainText(nl < 0 ? QString() : headMessage.mid(nl + 1).trimmed());
    }
}

void CommitPanel::updateButton()
{
    const int n = int(m_stagedFiles.size());
    const int len = int(m_summary->text().size());
    m_counter->setText(len ? QString::number(len) : QString());
    QPalette p = m_counter->palette();
    p.setColor(QPalette::WindowText, len > 72 ? theme::red : len > 50 ? theme::yellow : theme::faint);
    m_counter->setPalette(p);
    const QString to = m_branch.isEmpty() ? QString() : QStringLiteral(" to ") + m_branch;
    if (m_amend->isChecked()) m_commit->setText(n ? QStringLiteral("Amend with %1 %2").arg(n).arg(n == 1 ? "file" : "files") : QStringLiteral("Amend message"));
    else m_commit->setText(QStringLiteral("Commit %1 %2%3").arg(n).arg(n == 1 ? "file" : "files", to));
    m_commit->setEnabled(!m_summary->text().trimmed().isEmpty() && (n > 0 || m_amend->isChecked()));
}

void CommitPanel::triggerCommit()
{
    if (!m_commit->isEnabled()) return;
    QString msg = m_summary->text().trimmed();
    const QString body = m_body->toPlainText().trimmed();
    if (!body.isEmpty()) msg += QStringLiteral("\n\n") + body;
    emit commit(msg, m_amend->isChecked());
}

void CommitPanel::clearMessage()
{
    m_summary->clear();
    m_body->clear();
    m_amend->setChecked(false);
    saveDraft();
}

void CommitPanel::setRepoKey(const QString &repoPath)
{
    const QString key = QStringLiteral("drafts/")
                      + QString::fromLatin1(QCryptographicHash::hash(repoPath.toUtf8(), QCryptographicHash::Md5).toHex());
    if (key == m_draftKey) return;
    m_draftKey = key;
    QSettings s;
    const QSignalBlocker b1(m_summary), b2(m_body);
    m_summary->setText(s.value(key + "/summary").toString());
    m_body->setPlainText(s.value(key + "/body").toString());
    updateButton();
}

void CommitPanel::saveDraft()
{
    if (m_draftKey.isEmpty()) return;
    QSettings s;
    if (m_summary->text().isEmpty() && m_body->toPlainText().isEmpty()) {
        s.remove(m_draftKey);
        return;
    }
    s.setValue(m_draftKey + "/summary", m_summary->text());
    s.setValue(m_draftKey + "/body", m_body->toPlainText());
}
