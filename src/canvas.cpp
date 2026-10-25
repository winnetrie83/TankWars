#include "canvas.h"
#include "visibility.h"
#include <QJsonArray>
#include <QHash>
#include <QKeyEvent>
#include <QFont>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QTimer>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace tw {
static QPointF jsonPosition(const QJsonObject &j){return {j.value("x").toDouble(),j.value("y").toDouble()};}
GameCanvas::GameCanvas(QWidget *parent):QWidget(parent){
    setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setMinimumSize(460,330);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto *inputTimer=new QTimer(this);inputTimer->setTimerType(Qt::PreciseTimer);inputTimer->setInterval(33);
    connect(inputTimer,&QTimer::timeout,this,[this]{dispatch();});inputTimer->start();
    auto *redrawTimer=new QTimer(this);redrawTimer->setTimerType(Qt::PreciseTimer);redrawTimer->setInterval(16);
    connect(redrawTimer,&QTimer::timeout,this,[this]{if(isVisible())update();});redrawTimer->start();
    redrawClock_.start();
}
void GameCanvas::setMap(const ArenaMap &map){
    map_=map;current_={};previous_={};selectedPad_=-1;localTeam_=-1;selectionType_="player";selectionIndex_=-1;
    cameraInitialized_=false;camera_={map.width*Tile/2.0,map.height*Tile/2.0};
    visible_.fill('\0',map.width*map.height);explored_=visible_;
    update();
}
void GameCanvas::setSnapshot(const QJsonObject &snapshot){
    if(current_.isEmpty()&&!snapshot.isEmpty())cameraInitialized_=false;
    previous_=current_;current_=snapshot;frameClock_.restart();recomputeVision();update();
}
void GameCanvas::recomputeVision(){
    if(map_.terrain.isEmpty()||current_.isEmpty())return;
    QVector<VisionSource> sources;
    int team=localTeam_;
    for(const auto &entry:current_.value("players").toArray()){
        const auto j=entry.toObject();if(j.value("id").toInt()==localId_){team=j.value("team").toInt();break;}
    }
    if(team<0)return;
    if(localTeam_!=team){localTeam_=team;explored_.fill('\0',map_.width*map_.height);}
    for(const auto &entry:current_.value("players").toArray()){
        const auto j=entry.toObject();if(j.value("team").toInt()==team&&j.value("alive").toBool())sources.append(VisionSource{jsonPosition(j),9+3*j.value("radar").toInt()});
    }
    for(const auto &entry:current_.value("bases").toArray()){
        const auto j=entry.toObject();if(j.value("team").toInt()==team&&j.value("hp").toDouble()>0)sources.append(VisionSource{jsonPosition(j),8+3*j.value("radar").toInt()});
    }
    for(const auto &entry:current_.value("towers").toArray()){
        const auto j=entry.toObject();if(j.value("team").toInt()==team&&j.value("hp").toDouble()>0)sources.append(VisionSource{jsonPosition(j),7+2*j.value("range").toInt()});
    }
    ArenaMap sight=map_;
    for(const auto &value:current_.value("walls").toArray()){
        QPointF c=jsonPosition(value.toObject());QPoint tile(int(c.x()/Tile),int(c.y()/Tile));
        if(sight.validPoint(tile))sight.terrain[tile.y()*sight.width+tile.x()]=1;
    }
    visible_=visibleTiles(sight,sources);
    if(explored_.size()!=visible_.size())explored_.fill('\0',visible_.size());
    for(int k=0;k<visible_.size();++k)if(visible_.at(k))explored_[k]=1;
}
QColor GameCanvas::teamColor(int team)const{return team>=0&&team<colors_.size()?colors_[team]:QColor("#a8b5bd");}
QPointF GameCanvas::canvasPoint(QPointF point)const{
    return {(point.x()-offsetX_)/scale_,(point.y()-offsetY_)/scale_};
}
void GameCanvas::dispatch(){
    if(!onInput||current_.isEmpty())return;
    // Reproject the mouse every tick as the camera moves, avoiding stale world-space aiming.
    aim_=canvasPoint(mouse_);
    Input i;i.up=keys_.contains(controls_.up)||keys_.contains(Qt::Key_Up);
    i.down=keys_.contains(controls_.down)||keys_.contains(Qt::Key_Down);
    i.left=keys_.contains(controls_.left)||keys_.contains(Qt::Key_Left);
    i.right=keys_.contains(controls_.right)||keys_.contains(Qt::Key_Right);
    i.fire=shooting_||keys_.contains(controls_.fire);
    i.mine=keys_.contains(controls_.mine);i.wall=keys_.contains(controls_.wall);
    i.aimX=aim_.x();i.aimY=aim_.y();onInput(i);

}
void GameCanvas::keyPressEvent(QKeyEvent *e){if(!e->isAutoRepeat())keys_.insert(e->key());e->accept();}
void GameCanvas::keyReleaseEvent(QKeyEvent *e){if(!e->isAutoRepeat())keys_.remove(e->key());e->accept();}
void GameCanvas::mouseMoveEvent(QMouseEvent *e){mouse_=e->position();}
void GameCanvas::mousePressEvent(QMouseEvent *e){
    if(e->button()!=Qt::LeftButton&&e->button()!=Qt::RightButton)return;
    setFocus();mouse_=e->position();aim_=canvasPoint(mouse_);
    if(e->button()==Qt::LeftButton){shooting_=true;return;}
    QString type;int index=-1;double best=50;
    // Right-click to select objectives; left-click always fires (including at enemy bases).
    for(const auto &entry:current_.value("bases").toArray()){
        auto j=entry.toObject();QPointF c=jsonPosition(j);double d=std::hypot(c.x()-aim_.x(),c.y()-aim_.y());
        if(isVisibleAt(map_,visible_,c)&&d<best){best=d;type="base";index=j.value("team").toInt(-1);}
    }
    for(const auto &entry:current_.value("pads").toArray()){
        auto j=entry.toObject();QPointF c=jsonPosition(j);double d=std::hypot(c.x()-aim_.x(),c.y()-aim_.y());
        if(isVisibleAt(map_,visible_,c)&&d<best){best=d;type="pad";index=j.value("id").toInt(-1);}
    }
    for(int n=0;n<map_.towerPads.size();++n){
        QPoint tile=map_.towerPads[n];QPointF c((tile.x()+0.5)*Tile,(tile.y()+0.5)*Tile);
        double d=std::hypot(c.x()-aim_.x(),c.y()-aim_.y());
        if(isVisibleAt(map_,visible_,c)&&d<best){best=d;type="pad";index=n;}
    }
    for(const auto &entry:current_.value("nodes").toArray()){
        auto j=entry.toObject();QPointF c=jsonPosition(j);double d=std::hypot(c.x()-aim_.x(),c.y()-aim_.y());
        if(isVisibleAt(map_,visible_,c)&&d<best){best=d;type="node";index=j.value("id").toInt(-1);}
    }
    for(const auto &entry:current_.value("players").toArray()){
        auto j=entry.toObject();if(j.value("id").toInt()!=localId_||!j.value("alive").toBool())continue;
        QPointF c=jsonPosition(j);double d=std::hypot(c.x()-aim_.x(),c.y()-aim_.y());
        if(d<24&&d<best){best=d;type="player";index=-1;}
    }
    if(!type.isEmpty()){selectedPad_=type=="pad"?index:-1;selectionType_=type;selectionIndex_=index;
        shooting_=false;if(onSelection)onSelection(type,index);
    }
}
void GameCanvas::mouseReleaseEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton)shooting_=false;}
void GameCanvas::wheelEvent(QWheelEvent *e){
    double factor=e->angleDelta().y()>0?1.12:1.0/1.12;
    scale_=std::clamp(scale_*factor,0.7,2.5);e->accept();
}
void GameCanvas::focusOutEvent(QFocusEvent *){resetInput();}
QJsonObject GameCanvas::interpolated()const{
    if(previous_.isEmpty())return current_;
    // Network frames arrive every ~66 ms. One-frame interpolation removes positional jumping.
    double a=std::clamp(frameClock_.isValid()?frameClock_.elapsed()/66.0:1.0,0.0,1.0);
    QJsonObject frame=current_;
    for(const QString key:{QString("players"),QString("bullets")}){
        QHash<int,QJsonObject> old;
        for(const auto &v:previous_.value(key).toArray()){
            const auto j=v.toObject();old.insert(j.value("id").toInt(-1),j);
        }
        QJsonArray result;
        for(const auto &v:current_.value(key).toArray()){
            auto j=v.toObject();int id=j.value("id").toInt(-1);
            if(id>=0&&old.contains(id)&&(!j.contains("alive")||j.value("alive")==old[id].value("alive"))){
                const auto before=old.value(id);
                j["x"]=before.value("x").toDouble()*(1-a)+j.value("x").toDouble()*a;
                j["y"]=before.value("y").toDouble()*(1-a)+j.value("y").toDouble()*a;
                if(key=="players"){
                    double start=before.value("angle").toDouble(),end=j.value("angle").toDouble();
                    double delta=std::remainder(end-start,2*3.141592653589793);
                    j["angle"]=start+delta*a;
                }
            }
            result.append(j);
        }
        frame[key]=result;
    }
    return frame;
}
void GameCanvas::paintEvent(QPaintEvent *){
    QPainter p(this);p.fillRect(rect(),QColor("#071016"));
    if(map_.terrain.isEmpty())return;
    p.setRenderHint(QPainter::Antialiasing,true);
    const QJsonObject frame=interpolated();
    QPointF target=camera_;
    for(const auto &v:frame.value("players").toArray()){
        auto j=v.toObject();if(j.value("id").toInt()==localId_){
            target=jsonPosition(j);
            if(!j.value("alive").toBool()&&localTeam_>=0&&localTeam_<map_.bases.size())
                target=QPointF((map_.bases[localTeam_].x()+.5)*Tile,(map_.bases[localTeam_].y()+.5)*Tile);
            break;
        }
    }
    double dt=std::clamp(redrawClock_.restart()/1000.0,0.0,0.05);
    if(!cameraInitialized_){camera_=target;cameraInitialized_=true;}
    else camera_+=(target-camera_)*(1.0-std::exp(-10.0*dt));
    const double mapW=map_.width*Tile,mapH=map_.height*Tile;
    const double halfW=width()/(2*scale_),halfH=height()/(2*scale_);
    camera_.setX(mapW<=2*halfW?mapW/2:std::clamp(camera_.x(),halfW,mapW-halfW));
    camera_.setY(mapH<=2*halfH?mapH/2:std::clamp(camera_.y(),halfH,mapH-halfH));
    offsetX_=width()/2.0-camera_.x()*scale_;offsetY_=height()/2.0-camera_.y()*scale_;
    int x0=std::max(0,int(std::floor(-offsetX_/scale_/Tile))-1);
    int y0=std::max(0,int(std::floor(-offsetY_/scale_/Tile))-1);
    int x1=std::min(map_.width-1,int(std::ceil((width()-offsetX_)/scale_/Tile))+1);
    int y1=std::min(map_.height-1,int(std::ceil((height()-offsetY_)/scale_/Tile))+1);
    p.save();p.translate(offsetX_,offsetY_);p.scale(scale_,scale_);
    p.setClipRect(QRectF(0,0,mapW,mapH));
    p.setPen(Qt::NoPen);
    // Draw only camera-visible tiles, not the entire world.
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        int index=y*map_.width+x;
        if(explored_.size()!=map_.terrain.size()||!explored_.at(index))continue;
        int tile=map_.terrain[index];
        QColor col=tile==1?QColor("#59646a"):tile==2?QColor("#175c79"):
            ((x*13+y*7)%7<3?QColor("#315640"):QColor("#2a4a38"));
        QRectF r(x*Tile,y*Tile,Tile,Tile);p.fillRect(r,col);
        if(tile==1){p.setBrush(QColor("#87938c"));p.drawRoundedRect(r.adjusted(3,3,-4,-4),3,3);
            p.setPen(QPen(QColor("#334047"),1));p.drawLine(r.topLeft()+QPointF(7,8),r.bottomRight()-QPointF(6,8));p.setPen(Qt::NoPen);}
        else if(tile==2){p.setPen(QPen(QColor("#55a5b3"),1.3));p.drawArc(r.adjusted(3,9,-3,-9),0,180*16);p.setPen(Qt::NoPen);}
        else if((x*47+y*71)%19==0){p.setPen(QPen(QColor("#588d63"),1));
            p.drawLine(r.center(),r.center()+QPointF(3,-5));p.drawLine(r.center(),r.center()+QPointF(-3,-3));p.setPen(Qt::NoPen);}
        p.fillRect(QRectF(r.right()-1,r.top(),1,Tile),QColor(3,15,14,24));
        p.fillRect(QRectF(r.left(),r.bottom()-1,Tile,1),QColor(3,15,14,24));
    }
    // Static map markers stay recognisable in explored but currently unobserved territory.
    for(const auto &node:map_.nodes){
        QPoint tile=node.tile;int k=tile.y()*map_.width+tile.x();
        if(tile.x()<x0||tile.x()>x1||tile.y()<y0||tile.y()>y1)continue;
        if(explored_.size()<=k||!explored_.at(k)||visible_.at(k))continue;
        QPointF c((tile.x()+.5)*Tile,(tile.y()+.5)*Tile);
        p.setPen(QPen(QColor("#667676"),2));p.setBrush(QColor("#1d292d"));p.drawEllipse(c,33,33);
    }
    auto inView=[&](QPointF q,int pad=60){return q.x()>=x0*Tile-pad&&q.x()<(x1+1)*Tile+pad&&q.y()>=y0*Tile-pad&&q.y()<(y1+1)*Tile+pad;};
    auto visible=[&](QPointF q){return isVisibleAt(map_,visible_,q);};
    // Enlarged tower platforms with capture progress and team ownership.
    for(int idx=0;idx<map_.towerPads.size();++idx){
        QPoint tile=map_.towerPads[idx];QPointF c((tile.x()+.5)*Tile,(tile.y()+.5)*Tile);
        if(!inView(c))continue;
        int exploredIndex=tile.y()*map_.width+tile.x();
        if(exploredIndex<0||exploredIndex>=explored_.size()||!explored_.at(exploredIndex))continue;
        const bool observed=visible(c);
        QJsonObject state;
        for(const auto &entry:frame.value("pads").toArray())if(entry.toObject().value("id").toInt(-1)==idx){state=entry.toObject();break;}
        int owner=observed?state.value("owner").toInt(-1):-1,capturing=observed?state.value("capturing").toInt(-1):-1;
        QColor co=owner<0?QColor("#8da1a4"):teamColor(owner);
        p.setPen(QPen(idx==selectedPad_?QColor("#ffe28b"):co,idx==selectedPad_?4:3));
        p.setBrush(QColor("#243740"));p.drawRoundedRect(QRectF(c.x()-26,c.y()-26,52,52),6,6);
        p.setPen(QPen(QColor("#a9c1c9"),2));p.drawLine(c+QPointF(-17,0),c+QPointF(17,0));
        p.drawLine(c+QPointF(0,-17),c+QPointF(0,17));
        if(capturing>=0&&state.value("progress").toDouble()>0){
            p.setPen(QPen(teamColor(capturing),5));p.drawArc(QRectF(c.x()-32,c.y()-32,64,64),90*16,-int(state.value("progress").toDouble()/100*360*16));
        }
    }
    static const QStringList symbols={"O","Au","Fe","Cu","W","S","T"};
    for(const auto &v:frame.value("nodes").toArray()){
        auto j=v.toObject();QPointF c=jsonPosition(j);if(!inView(c)||!visible(c))continue;
        int owner=j.value("owner").toInt(-1),capturing=j.value("capturing").toInt(-1);
        QColor co=owner<0?QColor("#cbd4d9"):teamColor(owner);
        p.setPen(QPen(QColor("#091519"),4));p.setBrush(QColor("#172b31"));p.drawEllipse(c,38,38);
        p.setPen(QPen(co,2));p.setBrush(QColor("#243b3c"));p.drawEllipse(c,33,33);
        QFont font=p.font();font.setPixelSize(12);font.setBold(true);p.setFont(font);
        p.setPen(co);p.drawText(QRectF(c.x()-28,c.y()-15,56,30),Qt::AlignCenter,symbols.value(j.value("resource").toInt()));
        if(capturing>=0&&j.value("progress").toDouble()>0){p.setPen(QPen(teamColor(capturing),4));
            p.drawArc(QRectF(c.x()-43,c.y()-43,86,86),90*16,-int(j.value("progress").toDouble()/100*360*16));}
    }
    for(const auto &v:frame.value("bases").toArray()){
        auto j=v.toObject();QPointF c=jsonPosition(j);if(!inView(c)||!visible(c))continue;
        int team=j.value("team").toInt();double hp=j.value("hp").toDouble(),max=j.value("maxHp").toDouble();
        QColor co=teamColor(team);if(hp<=0)co.setAlpha(65);
        p.setPen(QPen(QColor("#08141a"),6));p.setBrush(QColor("#172630"));p.drawRoundedRect(QRectF(c.x()-50,c.y()-50,100,100),6,6);
        p.setPen(QPen(co,4));p.drawRoundedRect(QRectF(c.x()-43,c.y()-43,86,86),6,6);
        p.setBrush(co.darker(155));p.drawRoundedRect(QRectF(c.x()-26,c.y()-26,52,52),4,4);
        p.setPen(QPen(co.lighter(155),3));p.drawLine(c+QPointF(-24,0),c+QPointF(24,0));
        p.fillRect(QRectF(c.x()-50,c.y()-65,100,7),QColor("#131c22"));
        p.fillRect(QRectF(c.x()-50,c.y()-65,100*std::clamp(hp/std::max(1.0,max),0.0,1.0),7),co);
    }
    for(const auto &v:frame.value("towers").toArray()){
        auto j=v.toObject();QPointF c=jsonPosition(j);if(!inView(c)||!visible(c))continue;
        QColor co=teamColor(j.value("team").toInt());QString ty=j.value("type").toString();
        p.setPen(QPen(co,4));p.setBrush(QColor("#243b4b"));p.drawEllipse(c,30,30);
        p.setPen(QPen(ty=="Rocket"?QColor("#e68e5b"):ty=="Cannon"?QColor("#fee1a2"):QColor("#82dce1"),6));
        if(ty=="Rocket"){
            p.drawLine(c+QPointF(-9,8),c+QPointF(-9,-22));p.drawLine(c+QPointF(9,8),c+QPointF(9,-22));
        }else if(ty=="Cannon")p.drawLine(c,c+QPointF(0,-33));
        else for(int x=-8;x<=8;x+=8)p.drawLine(c+QPointF(x,8),c+QPointF(x,-28));
        p.setPen(Qt::NoPen);
    }
    for(const auto &v:frame.value("walls").toArray()){
        QPointF c=jsonPosition(v.toObject());if(!inView(c)||!visible(c))continue;
        QRectF r(c.x()-16,c.y()-16,32,32);p.setPen(QPen(QColor("#232e36"),2));p.setBrush(QColor("#82949a"));p.drawRoundedRect(r,2,2);
        p.setPen(QPen(QColor("#b2c0c5"),2));p.drawLine(r.left()+3,c.y(),r.right()-3,c.y());p.drawLine(c.x(),r.top()+3,c.x(),c.y());
    }
    for(const auto &v:frame.value("mines").toArray()){
        auto j=v.toObject();QPointF c=jsonPosition(j);if(!inView(c)||!visible(c))continue;
        QColor col=teamColor(j.value("team").toInt());col.setAlpha(110);
        p.setPen(QPen(col,2));p.setBrush(QColor(255,179,72,55));p.drawEllipse(c,10,10);
        p.drawLine(c+QPointF(-5,-5),c+QPointF(5,5));
    }
    for(const auto &v:frame.value("bullets").toArray()){
        QPointF c=jsonPosition(v.toObject());if(!inView(c)||!visible(c))continue;
        p.setPen(Qt::NoPen);p.setBrush(QColor(255,193,83,65));p.drawEllipse(c,9,9);
        p.setBrush(QColor("#fff0b3"));p.drawEllipse(c,3.8,3.8);
    }
    for(const auto &v:frame.value("players").toArray()){
        const auto j=v.toObject();if(!j.value("alive").toBool())continue;
        QPointF c=jsonPosition(j);if(!inView(c)||!visible(c))continue;
        int team=j.value("team").toInt();QColor co=teamColor(team);
        p.save();p.translate(c);p.rotate(j.value("angle").toDouble()*180/3.141592653589793);
        p.setPen(Qt::NoPen);p.setBrush(QColor(0,0,0,85));p.drawEllipse(QRectF(-19,-13,42,32));
        p.setPen(QPen(QColor("#0b161c"),2));p.setBrush(QColor("#1c252b"));
        p.drawRoundedRect(QRectF(-17,-15,10,30),3,3);p.drawRoundedRect(QRectF(7,-15,10,30),3,3);
        for(int ty=-9;ty<=9;ty+=6){p.setPen(QPen(QColor("#819091"),1));p.drawLine(-16,ty,-8,ty);p.drawLine(8,ty,16,ty);}
        p.setPen(QPen(co.darker(190),2));p.setBrush(co);
        const QString vehicle=j.value("vehicle").toString("Tank");
        if(vehicle=="Jeep"){
            p.drawRoundedRect(QRectF(-12,-9,24,18),4,4);
            p.setBrush(QColor("#1e3640"));p.drawRect(QRectF(-4,-8,9,16));
        }else if(vehicle=="Missile launcher"){
            p.drawRoundedRect(QRectF(-15,-11,30,22),3,3);
            p.setBrush(co.lighter(130));p.drawRect(QRectF(-1,-11,23,7));p.drawRect(QRectF(-1,4,23,7));
        }else p.drawRoundedRect(QRectF(-12,-12,24,24),4,4);
        p.setPen(QPen(QColor("#0b141b"),2));p.setBrush(co.lighter(135));p.drawRoundedRect(QRectF(0,-5,29,10),4,4);
        p.setBrush(co.darker(145));p.drawEllipse(QPointF(0,0),8,8);
        p.restore();
        p.setPen(j.value("id").toInt()==localId_?QColor("#ffe39b"):QColor("#edf6f4"));
        QFont font=p.font();font.setPixelSize(11);font.setBold(true);p.setFont(font);
        p.drawText(QRectF(c.x()-75,c.y()-34,150,16),Qt::AlignHCenter,j.value("name").toString());
        p.fillRect(QRectF(c.x()-17,c.y()+19,34,4),QColor("#101c1f"));
        p.fillRect(QRectF(c.x()-17,c.y()+19,34*std::clamp(j.value("hp").toDouble()/std::max(1.0,j.value("maxHp").toDouble()),0.0,1.0),4),QColor("#9ef2a3"));
        if(j.value("id").toInt()==localId_){p.setPen(QPen(QColor("#ffe39b"),1.2));p.setBrush(Qt::NoBrush);p.drawEllipse(c,24,24);}
    }
    // Fog overlays all static terrain; dynamic actors beyond vision were also removed by the server.
    p.setPen(Qt::NoPen);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        int k=y*map_.width+x;QRectF cell(x*Tile,y*Tile,Tile+0.5,Tile+0.5);
        if(visible_.size()<=k||!visible_.at(k)){
            p.fillRect(cell,explored_.size()>k&&explored_.at(k)?QColor(125,137,143,47):QColor("#03070b"));
        }
    }
    p.restore();
    // HUD and minimap are screen-space so zoom does not affect them.
    p.setPen(Qt::NoPen);p.setBrush(QColor(8,19,28,205));p.drawRoundedRect(QRect(13,13,258,36),8,8);
    p.setPen(QColor("#d5e3e8"));p.setFont(QFont("Segoe UI",9,QFont::DemiBold));
    p.drawText(QRect(20,13,240,36),Qt::AlignVCenter,QString("%1   •   %2 × %3   •   %4%").arg(map_.name).arg(map_.width).arg(map_.height).arg(int(scale_*100)));
    int mw=190,mh=std::max(70,int(190.0*map_.height/map_.width));
    QRect mini(width()-mw-16,16,mw,mh);p.fillRect(mini,QColor(5,10,17,215));
    if(explored_.size()==map_.width*map_.height){
        double tx=double(mw)/map_.width,ty=double(mh)/map_.height;
        for(int y=0;y<map_.height;++y)for(int x=0;x<map_.width;++x){
            int k=y*map_.width+x;if(!explored_.at(k))continue;
            bool live=visible_.size()>k&&visible_.at(k);
            int v=map_.terrain[k];p.fillRect(QRectF(mini.x()+x*tx,mini.y()+y*ty,tx+0.3,ty+0.3),
                live?(v==1?QColor("#829090"):v==2?QColor("#3485a0"):QColor("#4f8060")):
                (v==1?QColor("#283337"):QColor("#202e2c")));
        }
        for(const auto &v:frame.value("players").toArray()){
            auto j=v.toObject();if(!j.value("alive").toBool())continue;
            auto center=jsonPosition(j);if(!visible(center))continue;
            p.setBrush(teamColor(j.value("team").toInt()));
            p.drawEllipse(QPointF(mini.x()+center.x()/mapW*mw,mini.y()+center.y()/mapH*mh),3,3);
        }
        p.setPen(QPen(QColor("#fff7d4"),1));p.setBrush(Qt::NoBrush);
        QRectF viewport((0-offsetX_)/scale_/mapW*mw+mini.x(),(0-offsetY_)/scale_/mapH*mh+mini.y(),width()/scale_/mapW*mw,height()/scale_/mapH*mh);
        p.drawRect(viewport.intersected(QRectF(mini)));p.setPen(Qt::NoPen);
    }
    p.setPen(QPen(QColor("#607d88"),1));p.drawRect(mini);
    int win=frame.value("winner").toInt(-1);
    if(win>=0){p.fillRect(rect(),QColor(3,9,15,180));QFont f("Segoe UI",26,QFont::Bold);p.setFont(f);
        p.setPen(teamColor(win));p.drawText(rect(),Qt::AlignCenter,QString("TEAM %1 WINS!").arg(win+1));}
}
}
