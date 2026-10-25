#pragma once
#include "map.h"
#include <QByteArray>
#include <QPointF>
#include <QVector>
#include <cmath>

namespace tw {
struct VisionSource { QPointF pos; int radius=8; };
// Tile line of sight. Walls/water themselves are visible, but hide what's behind them.
inline bool hasLineOfSight(const ArenaMap &m,int x0,int y0,int x1,int y1){
    int dx=std::abs(x1-x0),dy=std::abs(y1-y0);
    int sx=(x0<x1)?1:-1,sy=(y0<y1)?1:-1,err=dx-dy;
    for(;;){
        if(x0==x1&&y0==y1)return true;
        if(!m.passable(x0,y0))return false;
        int e2=2*err;
        if(e2>-dy){err-=dy;x0+=sx;}
        if(e2<dx){err+=dx;y0+=sy;}
    }
}
inline QByteArray visibleTiles(const ArenaMap &map,const QVector<VisionSource> &sources){
    QByteArray mask(map.width*map.height,'\0');
    for(const auto &source:sources){
        int cx=int(std::floor(source.pos.x()/Tile)),cy=int(std::floor(source.pos.y()/Tile));
        int r=source.radius;
        for(int y=std::max(0,cy-r);y<std::min(map.height,cy+r+1);++y){
            for(int x=std::max(0,cx-r);x<std::min(map.width,cx+r+1);++x){
                if((x-cx)*(x-cx)+(y-cy)*(y-cy)>r*r)continue;
                int idx=y*map.width+x;
                if(!mask.at(idx)&&hasLineOfSight(map,cx,cy,x,y))mask[idx]=1;
            }
        }
    }
    return mask;
}
inline bool isVisibleAt(const ArenaMap &map,const QByteArray &mask,QPointF p){
    int x=int(std::floor(p.x()/Tile)),y=int(std::floor(p.y()/Tile));
    return map.validPoint({x,y})&&mask.at(y*map.width+x)!=0;
}
}
