#pragma once
// Application style: Fusion metrics with Xcode's look for scroll bars, menus, menu bar, tooltips, buttons, checkboxes.
#include <QProxyStyle>

class XcodeStyle : public QProxyStyle {
    Q_OBJECT
public:
    XcodeStyle();
    int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override;
    int styleHint(StyleHint h, const QStyleOption *o, const QWidget *w, QStyleHintReturn *r) const override;
    void drawPrimitive(PrimitiveElement e, const QStyleOption *o, QPainter *p, const QWidget *w) const override;
    void drawControl(ControlElement e, const QStyleOption *o, QPainter *p, const QWidget *w) const override;
    void drawComplexControl(ComplexControl c, const QStyleOptionComplex *o, QPainter *p, const QWidget *w) const override;
    QRect subControlRect(ComplexControl c, const QStyleOptionComplex *o, SubControl s, const QWidget *w) const override;
    QSize sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &s, const QWidget *w) const override;
    QRect subElementRect(SubElement e, const QStyleOption *o, const QWidget *w) const override;
    void polish(QWidget *w) override;
    using QProxyStyle::polish;
};
