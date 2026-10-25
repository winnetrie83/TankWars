#include "editor.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

namespace tw {
EditorCanvas::EditorCanvas(QWidget *parent):QWidget(parent){current=ArenaMap::standard();setMouseTracking(true);setMinimumSize(620,440);}
void EditorCanvas::recenter(){offsetX_=width()/2.0-current.width*Tile*scale_/2;offsetY_=height()/2.0-current.height*Tile*scale_/2;}
void EditorCanvas::paintEvent(QPaintEvent *){
    QPainter p(this);p.fillRect(rect(),QColor::fromRgb(0x0a1420));p.setRenderHint(QPainter::Antialiasing);
    // Fixed zoom / panning: big maps are edited piece by piece, never squeezed onto the screen.
    int x0=std::max(0,int(std::floor(-offsetX_/scale_/Tile))-1);
    int y0=std::max(0,int(std::floor(-offsetY_/scale_/Tile))-1);
    int x1=std::min(current.width-1,int(std::ceil((width()-offsetX_)/scale_/Tile))+1);
    int y1=std::min(current.height-1,int(std::ceil((height()-offsetY_)/scale_/Tile))+1);
    p.translate(offsetX_,offsetY_);p.scale(scale_,scale_);
    p.setClipRect(QRectF(0,0,current.width*Tile,current.height*Tile));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
        int v=current.terrain[y*current.width+x];p.fillRect(QRect(x*Tile,y*Tile,Tile,Tile),
            v==1?QColor::fromRgb(0x808891):v==2?QColor::fromRgb(0x286a8d):QColor::fromRgb(0x31523a));
        p.setPen(QPen(QColor(0,0,0,45),1));p.drawRect(QRect(x*Tile,y*Tile,Tile,Tile));
    }
    p.setPen(Qt::NoPen);
    for(int t=0;t<current.bases.size();++t){
        auto tile=current.bases[t];if(tile.x()<x0-2||tile.x()>x1+2||tile.y()<y0-2||tile.y()>y1+2)continue;
        QPointF c((tile.x()+.5)*Tile,(tile.y()+.5)*Tile);
        QColor co=QColor::fromHsv((t*75+200)%360,230,250);
        QColor fill=co;fill.setAlpha(65);p.setPen(QPen(co,3));p.setBrush(fill);
        p.drawRoundedRect(QRectF(c.x()-50,c.y()-50,100,100),8,8);
        p.setPen(co);p.drawText(QRectF(c.x()-20,c.y()-15,40,30),Qt::AlignCenter,QString::number(t+1));
    }
    static const QStringList letters={"O","Au","Fe","Cu","W","S","T"};
    for(const auto &node:std::as_const(current.nodes)){
        QPoint tile=node.tile;if(tile.x()<x0-2||tile.x()>x1+2||tile.y()<y0-2||tile.y()>y1+2)continue;
        QPointF c((tile.x()+.5)*Tile,(tile.y()+.5)*Tile);
        p.setPen(QPen(QColor::fromRgb(0xf2d18b),2));p.setBrush(QColor(242,209,139,65));p.drawEllipse(c,38,38);
        p.setPen(QColor::fromRgb(0xf7e6c0));p.drawText(QRectF(c.x()-20,c.y()-12,40,24),Qt::AlignCenter,letters.value(node.type));
    }
    for(QPoint tile:std::as_const(current.towerPads)){
        if(tile.x()<x0-2||tile.x()>x1+2||tile.y()<y0-2||tile.y()>y1+2)continue;
        int owner=-1;
        for(int team=0;team<current.bases.size();++team){
            QPoint b=current.bases[team];int dx=tile.x()-b.x(),dy=tile.y()-b.y();
            if(dx*dx+dy*dy<=36){owner=team;break;}
        }
        QColor color=owner<0?QColor::fromRgb(0xd3e6ef):QColor::fromHsv((owner*75+200)%360,230,250);
        QPointF c((tile.x()+.5)*Tile,(tile.y()+.5)*Tile);
        QColor fill=color;fill.setAlpha(55);p.setPen(QPen(color,3));p.setBrush(fill);
        p.drawRoundedRect(QRectF(c.x()-26,c.y()-26,52,52),6,6);
        p.setPen(color);p.drawLine(c+QPointF(-15,0),c+QPointF(15,0));p.drawLine(c+QPointF(0,-15),c+QPointF(0,15));
        if(owner>=0)p.drawText(QRectF(c.x()-16,c.y()+11,32,16),Qt::AlignCenter,QString::number(owner+1));
    }
}
void EditorCanvas::paintAt(QPointF pos){
    int x=int(std::floor((pos.x()-offsetX_)/scale_/Tile)),y=int(std::floor((pos.y()-offsetY_)/scale_/Tile));
    QPoint target(x,y);if(!current.validPoint(target))return;
    // Resource, pads and bases are mutually exclusive with each other.
    auto eraseObjects=[&]{
        for(int k=current.nodes.size()-1;k>=0;--k)if(current.nodes[k].tile==target)current.nodes.removeAt(k);
        current.towerPads.removeAll(target);
    };
    if(tool<=2){
        current.terrain[y*current.width+x]=tool;
        if(tool!=0){eraseObjects();for(auto p:std::as_const(current.bases))if(p==target){current.terrain[y*current.width+x]=0;break;}}
    }else if(tool==3){eraseObjects();}
    else if(tool>=4&&tool<=10){
        current.terrain[y*current.width+x]=0;eraseObjects();
        bool baseHere=false;for(auto p:std::as_const(current.bases))if(p==target)baseHere=true;
        if(!baseHere)current.nodes.append({target,tool-4});
    }else if(tool==11){
        current.terrain[y*current.width+x]=0;eraseObjects();
        bool baseHere=false;for(auto p:std::as_const(current.bases))if(p==target)baseHere=true;
        if(!baseHere)current.towerPads.append(target);
    }else if(tool>=12&&tool<=15){
        current.terrain[y*current.width+x]=0;eraseObjects();
        int base=tool-12;
        if(!current.bases.contains(target)||current.bases[base]==target)current.bases[base]=target;
    }
    update();
}
void EditorCanvas::mousePressEvent(QMouseEvent *event){
    if(event->button()==Qt::MiddleButton||event->button()==Qt::RightButton){panning_=true;panStart_=event->position();setCursor(Qt::ClosedHandCursor);}
    else if(event->button()==Qt::LeftButton)paintAt(event->position());
}
void EditorCanvas::mouseMoveEvent(QMouseEvent *event){
    if(panning_){QPointF delta=event->position()-panStart_;offsetX_+=delta.x();offsetY_+=delta.y();panStart_=event->position();update();}
    else if(event->buttons()&Qt::LeftButton&&tool<=3)paintAt(event->position());
}
void EditorCanvas::mouseReleaseEvent(QMouseEvent *event){
    if(event->button()==Qt::MiddleButton||event->button()==Qt::RightButton){panning_=false;unsetCursor();}
}
void EditorCanvas::wheelEvent(QWheelEvent *event){
    QPointF mouse=event->position();double before=scale_;
    scale_=std::clamp(scale_*(event->angleDelta().y()>0?1.15:1/1.15),0.3,2.5);
    offsetX_=mouse.x()-(mouse.x()-offsetX_)*scale_/before;
    offsetY_=mouse.y()-(mouse.y()-offsetY_)*scale_/before;
    update();event->accept();
}
MapEditor::MapEditor(QWidget *parent):QDialog(parent){
    setWindowTitle("TankWars | Map Editor");resize(1160,760);
    auto *root=new QHBoxLayout(this);canvas_=new EditorCanvas;root->addWidget(canvas_,1);
    auto *side=new QVBoxLayout;root->addLayout(side);
    auto *heading=new QLabel("<h2>MAP EDITOR</h2>");side->addWidget(heading);
    side->addWidget(new QLabel("Left-click: paint / place    •    Wheel: zoom\nRight/Middle-drag: move around the map."));
    tools_=new QComboBox;tools_->addItems({"Grass / passable","Stone wall / impassable","Water / impassable",
        "Erase object","Platform: Oil","Platform: Gold","Platform: Iron","Platform: Copper",
        "Platform: Wood","Platform: Stone","Platform: Tech","Tower pad",
        "Base: Team 1","Base: Team 2","Base: Team 3","Base: Team 4"});
    tools_->setMaxVisibleItems(20);side->addWidget(tools_);
    connect(tools_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){canvas_->tool=i;});
    side->addSpacing(14);
    side->addWidget(new QLabel("<b>NEW MAP SIZE</b>"));
    auto *sizeRow=new QHBoxLayout;side->addLayout(sizeRow);
    mapWidth_=new QSpinBox;mapWidth_->setRange(20,192);mapWidth_->setValue(96);mapWidth_->setPrefix("W: ");
    mapHeight_=new QSpinBox;mapHeight_->setRange(16,128);mapHeight_->setValue(64);mapHeight_->setPrefix("H: ");
    sizeRow->addWidget(mapWidth_);sizeRow->addWidget(mapHeight_);
    side->addSpacing(14);
    side->addWidget(new QLabel("<b>MAP RULES</b>"));
    auto *help=new QLabel("• 20–192 columns, 16–128 rows\n• Four base positions (two or four active)\n• At least 8 capture platforms\n• At least 4 tower pads\n• Pads within 6 tiles of a base start owned\n• Enlarged marker outlines show footprints\n• Objects cannot overlap\n• All resource platforms must be reachable\n  from all four bases\n• Walls and water block tanks/shots\n• Saves as a portable JSON map");
    help->setWordWrap(true);side->addWidget(help);
    auto *clear=new QPushButton("New blank map");side->addWidget(clear);connect(clear,&QPushButton::clicked,this,&MapEditor::blank);
    auto *openButton=new QPushButton("Open map...");side->addWidget(openButton);connect(openButton,&QPushButton::clicked,this,&MapEditor::open);
    auto *saveButton=new QPushButton("Validate & save...");side->addWidget(saveButton);connect(saveButton,&QPushButton::clicked,this,&MapEditor::save);
    auto *done=new QPushButton("Use this map in lobby");side->addWidget(done);
    connect(done,&QPushButton::clicked,this,[this]{QString error;
        if(!canvas_->current.validate(4,&error)){QMessageBox::warning(this,"Map validation",error);return;}
        accept();
    });
    side->addStretch();
}
void MapEditor::setMap(const ArenaMap &map){
    canvas_->current=map;mapWidth_->setValue(map.width);mapHeight_->setValue(map.height);canvas_->centerMap();
}
void MapEditor::blank(){
    if(QMessageBox::question(this,"New map","Clear terrain and resource/tower markers?")!=QMessageBox::Yes)return;
    ArenaMap m;m.width=mapWidth_->value();m.height=mapHeight_->value();
    m.terrain.fill(0,m.width*m.height);
    m.bases={{3,3},{m.width-4,m.height-4},{m.width-4,3},{3,m.height-4}};
    m.nodes.clear();m.towerPads.clear();m.name="Untitled";
    canvas_->current=m;canvas_->centerMap();
}
void MapEditor::open(){
    QString path=QFileDialog::getOpenFileName(this,"Open TankWars map",mapDirectory(),"TankWars maps (*.json)");
    if(path.isEmpty())return;
    ArenaMap loaded;QString error;
    if(!loadMap(path,loaded,&error)){QMessageBox::warning(this,"Invalid map",error);return;}
    canvas_->current=loaded;mapWidth_->setValue(loaded.width);mapHeight_->setValue(loaded.height);canvas_->centerMap();
}
void MapEditor::save(){
    QString error;if(!canvas_->current.validate(4,&error)){QMessageBox::warning(this,"Map validation",error);return;}
    bool ok=false;QString name=QInputDialog::getText(this,"Map name","Custom map name:",QLineEdit::Normal,canvas_->current.name,&ok).trimmed();
    if(!ok||name.isEmpty())return;
    canvas_->current.name=name.left(60);
    QString path=mapDirectory()+"/"+cleanMapFilename(canvas_->current.name);
    if(QFileInfo::exists(path)&&QMessageBox::question(this,"Overwrite map","Overwrite "+path+"?")!=QMessageBox::Yes)return;
    if(!saveMap(canvas_->current,path,&error))QMessageBox::warning(this,"Save failed",error);
    else QMessageBox::information(this,"Saved","Map saved to:\n"+path);
}
}
