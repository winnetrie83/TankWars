#pragma once
#include "map.h"
#include "world.h"
#include "controls.h"
#include <QColor>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QWidget>
#include <QSet>
#include <functional>

namespace tw {
class GameCanvas : public QWidget {
public:
    explicit GameCanvas(QWidget *parent=nullptr);
    void setMap(const ArenaMap &map);
    void setSnapshot(const QJsonObject &snapshot);
    void setLocalId(int id){localId_=id;}
    void setColors(const QVector<QColor> &colors){colors_=colors;update();}
    void setControls(const Controls &controls){controls_=controls;resetInput();}
    int selectedPad()const{return selectedPad_;}
    void selectPlayer(){selectedPad_=-1;selectionType_="player";selectionIndex_=-1;if(onSelection)onSelection("player",-1);}
    QString selectionType()const{return selectionType_;}
    int selectionIndex()const{return selectionIndex_;}
    void resetInput(){keys_.clear();shooting_=false;}
    std::function<void(const Input &)> onInput;
    std::function<void(const QString &,int)> onSelection;
protected:
    void paintEvent(QPaintEvent *)override;
    void keyPressEvent(QKeyEvent *)override;
    void keyReleaseEvent(QKeyEvent *)override;
    void mouseMoveEvent(QMouseEvent *)override;
    void mousePressEvent(QMouseEvent *)override;
    void mouseReleaseEvent(QMouseEvent *)override;
    void wheelEvent(QWheelEvent *)override;
    void focusOutEvent(QFocusEvent *)override;
private:
    ArenaMap map_;
    QJsonObject current_,previous_;
    QVector<QColor> colors_={QColor("#35a8ff"),QColor("#ff6268"),QColor("#79d38c"),QColor("#f5be54")};
    Controls controls_=Controls::load();
    QSet<int> keys_;
    QByteArray visible_,explored_;
    QPointF mouse_,aim_,camera_;
    bool shooting_=false,cameraInitialized_=false;
    int selectedPad_=-1,selectionIndex_=-1,localId_=-1,localTeam_=-1;
    QString selectionType_="player";
    double scale_=1.35,offsetX_=0,offsetY_=0;
    QElapsedTimer frameClock_,redrawClock_;
    QPointF canvasPoint(QPointF p)const;
    QColor teamColor(int team)const;
    void dispatch();
    QJsonObject interpolated()const;
    void recomputeVision();
};
}
