#pragma once
#include <QColor>
#include <QFont>

class QPainter;
class QRectF;

namespace theme {
inline const QColor bg{0x1b, 0x1d, 0x23};
inline const QColor graphBg{0x17, 0x19, 0x1e};
inline const QColor panel{0x21, 0x24, 0x2b};
inline const QColor sidebar{0x1e, 0x21, 0x27};
inline const QColor topbar{0x15, 0x17, 0x1b};
inline const QColor border{0x2b, 0x2f, 0x37};
inline const QColor text{0xd8, 0xdc, 0xe3};
inline const QColor dim{0x8d, 0x94, 0xa1};
inline const QColor faint{0x5c, 0x63, 0x70};
inline const QColor accent{0x3f, 0xa2, 0xf7};
inline const QColor selection{0x28, 0x37, 0x4b};
inline const QColor hover{0x23, 0x27, 0x30};
inline const QColor field{0x27, 0x2b, 0x33};

inline const QColor addBg{0x1b, 0x33, 0x24};
inline const QColor delBg{0x3a, 0x21, 0x27};
inline const QColor addGutter{0x1f, 0x3f, 0x2b};
inline const QColor delGutter{0x4a, 0x26, 0x2d};
inline const QColor addMark{0x2e, 0x7d, 0x43};
inline const QColor delMark{0x8f, 0x34, 0x34};
inline const QColor hunkBg{0x1d, 0x26, 0x35};
inline const QColor filler{0x1e, 0x20, 0x26};
inline const QColor gutter{0x1a, 0x1c, 0x21};

inline const QColor green{0x4c, 0xc2, 0x6b};
inline const QColor red{0xe5, 0x53, 0x4b};
inline const QColor yellow{0xe0, 0xa4, 0x3a};
inline const QColor blue{0x4c, 0x9f, 0xe0};

QColor lane(int i);
QColor statusColor(const QString &s);
QFont uiFont(int px, bool bold = false);
QFont monoFont(int px);
QString styleSheet();

// Small vector icons, drawn in the given color inside r.
enum class Icon { Laptop, Cloud, Tag, Branch, Stash };
void paintIcon(QPainter *p, Icon icon, const QRectF &r, const QColor &c);
} // namespace theme
