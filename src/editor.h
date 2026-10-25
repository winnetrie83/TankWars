#pragma once
#include "map.h"
#include <QDialog>
#include <QComboBox>
#include <QWidget>
#include <QSpinBox>
#include <QPointF>

namespace tw {
class EditorCanvas : public QWidget {
public:
    explicit EditorCanvas(QWidget *parent=nullptr);
    ArenaMap current;
    int tool=0;
    void refresh(){update();}
protected:
    void paintEvent(QPaintEvent *)override;
    void mousePressEvent(QMouseEvent *)override;
    void mouseMoveEvent(QMouseEvent *)override;
    void mouseReleaseEvent(QMouseEvent *)override;
    void wheelEvent(QWheelEvent *)override;
private:
    void paintAt(QPointF pos);
    double scale_=0.95,offsetX_=0,offsetY_=0;
    QPointF panStart_;bool panning_=false;
    void recenter();
public:
    void centerMap(){recenter();update();}
};
class MapEditor : public QDialog {
public:
    explicit MapEditor(QWidget *parent=nullptr);
    ArenaMap edited()const{return canvas_->current;}
    void setMap(const ArenaMap &map);
private:
    EditorCanvas *canvas_;
    QComboBox *tools_;
    QSpinBox *mapWidth_=nullptr,*mapHeight_=nullptr;
    void save();
    void open();
    void blank();
};
}
