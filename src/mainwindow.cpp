#include "mainwindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMessageBox>
#include <QScrollArea>
#include <QDialog>
#include <QKeySequenceEdit>
#include <QSlider>
#include <QSettings>
#include <array>
#include <QSignalBlocker>
#include <QSet>
#include <cmath>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

namespace tw {
static QLabel *heading(const QString &text){auto *l=new QLabel(text);l->setObjectName("heading");return l;}
static QPushButton *button(const QString &label){auto *b=new QPushButton(label);b->setMinimumHeight(36);return b;}
static QTableWidget *table(int columns,const QStringList &labels){
    auto *t=new QTableWidget(0,columns);t->setHorizontalHeaderLabels(labels);
    t->setSelectionMode(QAbstractItemView::NoSelection);t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setAlternatingRowColors(true);return t;
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent){
    setWindowTitle("TANKWARS | Network Arena");resize(1420,890);
    setMinimumSize(1020,650);
    setStyleSheet(R"(
        QMainWindow,QWidget {background:#0b1721;color:#e4eef4;font:10pt 'Segoe UI';}
        QStackedWidget {background:#0b1721;}
        QGroupBox {background:#142431;border:1px solid #34485a;border-radius:11px;margin-top:15px;padding:14px;font-weight:bold;}
        QGroupBox::title {subcontrol-origin:margin;left:12px;padding:0 5px;color:#adc6dc;}
        QLabel#heading {font:bold 21pt 'Segoe UI';color:#ffe2a2;}
        QPushButton {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #365872,stop:1 #213b52);border:1px solid #496e86;border-radius:8px;padding:7px 12px;font-weight:600;}
        QPushButton:hover {background:#366788;} QPushButton:disabled {background:#26323d;color:#788995;border-color:#26323d;}
        QPushButton#primary {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ce963e,stop:1 #875b26);color:#fff7ea;border-color:#daaa67;}
        QPushButton#primary:hover {background:#ca8a31;}
        QLineEdit,QSpinBox,QComboBox {background:#172b3b;border:1px solid #476178;border-radius:5px;padding:7px;}
        QTableWidget {background:#182939;alternate-background-color:#1f3345;border:1px solid #395066;gridline-color:#334b5c;}
        QHeaderView::section {background:#293e51;color:#f1cf8d;border:0;padding:7px;font-weight:bold;}
        QProgressBar {background:#132634;border:1px solid #38526b;border-radius:5px;text-align:center;}
        QProgressBar::chunk {background:#48b6cb;border-radius:4px;}
        QCheckBox {spacing:8px;}
    )");
    pages_=new QStackedWidget;setCentralWidget(pages_);
    constructHome();constructLobby();constructLoading();constructGame();canvas_->setControls(controls_);
    connect(&session_,&Session::changed,this,&MainWindow::updateUi);
    connect(&session_,&Session::notice,this,[this](const QString &txt){statusBar()->showMessage(txt,14000);if(session_.phase()==Session::Phase::Home)QMessageBox::warning(this,"Network message",txt);});
    connect(&session_,&Session::mapAvailable,this,&MainWindow::startCanvas);
    connect(&session_,&Session::frame,this,[this](const QJsonObject &snap){
        if(!lastFrame_.isEmpty()){
            auto findSelf=[this](const QJsonObject &j)->QJsonObject{
                for(const auto &v:j.value("players").toArray())if(v.toObject().value("id").toInt()==session_.myId())return v.toObject();
                return {};
            };
            auto prev=findSelf(lastFrame_),cur=findSelf(snap);
            if(!prev.isEmpty()&&!cur.isEmpty()){
                if(cur.value("shots").toInt()>prev.value("shots").toInt()){
                    const QString vehicle=cur.value("vehicle").toString();
                    sounds_.play(vehicle=="Jeep"?"jeep_mg":vehicle=="Missile launcher"?"rocket":"cannon");
                }
                if(prev.value("alive").toBool()&&!cur.value("alive").toBool())sounds_.play("blast");
                else if(cur.value("hp").toDouble()<prev.value("hp").toDouble())sounds_.play("hit");
            }
            // Fire audio is based on server-side shot counters, never local input polling.
            // Other visible vehicles and towers contribute localized sound events too.
            const QPointF listener(cur.value("x").toDouble(),cur.value("y").toDouble());
            auto nearby=[&](const QJsonObject &j){
                return std::hypot(j.value("x").toDouble()-listener.x(),j.value("y").toDouble()-listener.y())<560;
            };
            QHash<int,int> oldPlayerShots;
            for(const auto &v:lastFrame_.value("players").toArray()){
                const auto j=v.toObject();oldPlayerShots[j.value("id").toInt()]=j.value("shots").toInt();
            }
            for(const auto &v:snap.value("players").toArray()){
                const auto j=v.toObject();int id=j.value("id").toInt();
                if(id==session_.myId()||!oldPlayerShots.contains(id)||!nearby(j))continue;
                if(j.value("shots").toInt()>oldPlayerShots.value(id)){
                    const QString vehicle=j.value("vehicle").toString();
                    sounds_.play(vehicle=="Jeep"?"jeep_mg":vehicle=="Missile launcher"?"rocket":"cannon");
                }
            }
            QHash<int,int> oldTowerShots;
            for(const auto &v:lastFrame_.value("towers").toArray()){
                const auto j=v.toObject();oldTowerShots[j.value("id").toInt()]=j.value("shots").toInt();
            }
            for(const auto &v:snap.value("towers").toArray()){
                const auto j=v.toObject();int turretId=j.value("id").toInt();
                if(!nearby(j))continue;
                if(j.value("shots").toInt()>oldTowerShots.value(turretId,0)){
                    const QString type=j.value("type").toString();
                    sounds_.play(type=="Rocket"?"rocket":type=="Minigun"?"minigun":"cannon");
                }
            }
            QSet<int> oldExplosions;
            for(const auto &v:lastFrame_.value("explosions").toArray())oldExplosions.insert(v.toObject().value("id").toInt());
            for(const auto &v:snap.value("explosions").toArray()){
                const auto j=v.toObject();if(oldExplosions.contains(j.value("id").toInt())||!nearby(j))continue;
                sounds_.play(j.value("kind").toString()=="Mine"?"mine_blast":"rocket_blast");
            }
            QSet<QString> previousOwned;
            int team=cur.value("team").toInt(-1);
            for(const auto &v:lastFrame_.value("nodes").toArray()){
                auto j=v.toObject();if(j.value("owner").toInt(-1)==team)
                    previousOwned.insert(QString::number(j.value("x").toInt())+":"+QString::number(j.value("y").toInt()));
            }
            if(team>=0)for(const auto &v:snap.value("nodes").toArray()){
                auto j=v.toObject();QString key=QString::number(j.value("x").toInt())+":"+QString::number(j.value("y").toInt());
                if(j.value("owner").toInt(-1)==team&&!previousOwned.contains(key)){
                    sounds_.play("capture");break;
                }
            }
        }
        lastFrame_=snap;canvas_->setSnapshot(snap);updateHud(snap);updateUpgrades(snap);
    });
    statusBar()->showMessage("Ready. Host a lobby or join by IP address.");
    updateUi();
}
void MainWindow::constructHome(){
    home_=new QWidget;pages_->addWidget(home_);
    auto *outer=new QVBoxLayout(home_);outer->setContentsMargins(80,48,80,48);outer->addStretch();
    outer->addWidget(heading("T A N K W A R S"),0,Qt::AlignCenter);
    auto *subtitle=new QLabel("MULTIPLAYER  •  TEAM BASES  •  RESOURCE WARFARE");
    subtitle->setAlignment(Qt::AlignCenter);outer->addWidget(subtitle);
    outer->addSpacing(20);
    auto *box=new QGroupBox("CONNECT TO THE ARENA");box->setMaximumWidth(580);
    auto *body=new QVBoxLayout(box);
    auto *fields=new QFormLayout;
    name_=new QLineEdit("Commander");name_->setMaxLength(24);
    ip_=new QLineEdit("127.0.0.1");port_=new QSpinBox;port_->setRange(1024,65535);port_->setValue(45454);
    fields->addRow("Player name",name_);fields->addRow("Host IP / address",ip_);fields->addRow("TCP port",port_);
    body->addLayout(fields);
    auto *buttons=new QHBoxLayout;
    auto *create=button("HOST A GAME"),*joinButton=button("JOIN GAME");create->setObjectName("primary");
    buttons->addWidget(create);buttons->addWidget(joinButton);body->addLayout(buttons);
    connect(create,&QPushButton::clicked,this,&MainWindow::host);
    connect(joinButton,&QPushButton::clicked,this,&MainWindow::join);
    auto *edit=button("OPEN MAP EDITOR");body->addWidget(edit);
    connect(edit,&QPushButton::clicked,this,[this]{openEditor(false);});
    auto *settings=button("CONTROLS & AUDIO SETTINGS");body->addWidget(settings);
    connect(settings,&QPushButton::clicked,this,&MainWindow::preferences);
    auto *hint=new QLabel("LAN: join using the host computer's IPv4 address.\n"
        "The host must allow the TCP port through the firewall.\n"
        "For internet play, use a trusted VPN or configure TCP port forwarding.");
    hint->setWordWrap(true);body->addWidget(hint);
    auto *center=new QHBoxLayout;center->addStretch();center->addWidget(box);center->addStretch();outer->addLayout(center);outer->addStretch();
}
void MainWindow::constructLobby(){
    lobby_=new QWidget;pages_->addWidget(lobby_);
    auto *layout=new QVBoxLayout(lobby_);layout->setContentsMargins(22,20,22,20);
    lobbyHeader_=heading("MULTIPLAYER LOBBY");layout->addWidget(lobbyHeader_);
    mapLabel_=new QLabel("Map: Crossroads");layout->addWidget(mapLabel_);
    auto *body=new QHBoxLayout;layout->addLayout(body,1);
    auto *left=new QVBoxLayout;body->addLayout(left,3);
    lobbyPlayers_=table(5,{"Player","Team","Type / strategy","Ready","Status"});left->addWidget(lobbyPlayers_,1);
    auto *controls=new QHBoxLayout;left->addLayout(controls);
    ready_=button("I'M READY");ready_->setObjectName("primary");controls->addWidget(ready_);
    connect(ready_,&QPushButton::clicked,this,[this]{
        auto id=session_.myId();for(const auto &p:session_.players())if(p.id==id){session_.changeReady(!p.ready);break;}
    });
    teamChoice_=new QComboBox;controls->addWidget(teamChoice_);
    vehicleChoice_=new QComboBox;vehicleChoice_->addItems({"Jeep","Tank","Missile launcher"});controls->addWidget(vehicleChoice_);
    connect(vehicleChoice_,&QComboBox::currentTextChanged,this,[this](const QString &v){session_.changeVehicle(v);});
    connect(teamChoice_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int t){if(t>=0)session_.changeTeam(t);});
    auto *back=button("LEAVE LOBBY");controls->addWidget(back);
    connect(back,&QPushButton::clicked,this,[this]{session_.leave();updateUi();});
    left->addWidget(new QLabel("The host may start only when every human player is READY.\n"
        "Bots count toward the 8-player limit and are always ready. Each team needs a participant."));
    auto *right=new QVBoxLayout;body->addLayout(right,2);
    auto *group=new QGroupBox("HOST GAME SETTINGS");right->addWidget(group);
    auto *form=new QFormLayout(group);
    teams_=new QComboBox;teams_->addItems({"2 teams","4 teams"});form->addRow("Team count",teams_);
    capture_=new QSpinBox;capture_->setRange(3,15);capture_->setSuffix(" sec");capture_->setValue(6);form->addRow("Capture time",capture_);
    supply_=new QSpinBox;supply_->setRange(1,3);supply_->setSuffix("x");form->addRow("Starting supplies",supply_);
    respawn_=new QSpinBox;respawn_->setRange(5,30);respawn_->setSuffix(" Oil + Iron");respawn_->setValue(10);form->addRow("Respawn price",respawn_);
    friendly_=new QCheckBox("Allow friendly fire");form->addRow(friendly_);
    connect(teams_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::setOptions);
    connect(capture_,QOverload<int>::of(&QSpinBox::valueChanged),this,&MainWindow::setOptions);
    connect(supply_,QOverload<int>::of(&QSpinBox::valueChanged),this,&MainWindow::setOptions);
    connect(respawn_,QOverload<int>::of(&QSpinBox::valueChanged),this,&MainWindow::setOptions);
    connect(friendly_,&QCheckBox::toggled,this,&MainWindow::setOptions);
    auto *botGroup=new QGroupBox("AI PARTICIPANTS");right->addWidget(botGroup);
    auto *botLayout=new QVBoxLayout(botGroup);
    botType_=new QComboBox;botType_->addItems({"Collector","Aggressive","Defender"});botLayout->addWidget(botType_);
    auto *botRow=new QHBoxLayout;botLayout->addLayout(botRow);
    addBot_=button("+ ADD BOT");removeBot_=button("− REMOVE BOT");botRow->addWidget(addBot_);botRow->addWidget(removeBot_);
    connect(addBot_,&QPushButton::clicked,this,[this]{session_.addBot(botType_->currentText());});
    connect(removeBot_,&QPushButton::clicked,&session_,&Session::removeBot);
    auto *mapGroup=new QGroupBox("CUSTOM MAP");right->addWidget(mapGroup);
    auto *mapLayout=new QVBoxLayout(mapGroup);
    openMap_=button("CHOOSE SAVED MAP");editMap_=button("MAP EDITOR");mapLayout->addWidget(openMap_);mapLayout->addWidget(editMap_);
    connect(openMap_,&QPushButton::clicked,this,&MainWindow::selectMap);
    connect(editMap_,&QPushButton::clicked,this,[this]{openEditor(true);});
    right->addStretch();
    start_=button("START MATCH  •  5 SECOND COUNTDOWN");start_->setObjectName("primary");
    layout->addWidget(start_);connect(start_,&QPushButton::clicked,&session_,&Session::startMatch);
}
void MainWindow::constructLoading(){
    loading_=new QWidget;pages_->addWidget(loading_);
    auto *layout=new QVBoxLayout(loading_);layout->setContentsMargins(70,48,70,48);
    loadingHeader_=heading("PREPARING BATTLE");layout->addWidget(loadingHeader_);
    loadingHint_=new QLabel("Waiting for everybody to finish loading...");layout->addWidget(loadingHint_);
    loadingPlayers_=table(4,{"Player","Status / reason","Download","Loaded"});
    layout->addWidget(loadingPlayers_,1);
    auto *quit=button("DISCONNECT");layout->addWidget(quit);
    connect(quit,&QPushButton::clicked,this,[this]{session_.leave();updateUi();});
}
void MainWindow::constructGame(){
    game_=new QWidget;pages_->addWidget(game_);
    auto *layout=new QHBoxLayout(game_);layout->setContentsMargins(9,9,9,9);
    canvas_=new GameCanvas;layout->addWidget(canvas_,1);
    canvas_->onInput=[this](const Input &in){session_.sendInput(in);};
    canvas_->onBuildWall=[this](QPointF point){session_.buildWallAt(point);canvas_->setFocus();};
    canvas_->onWallModeChanged=[this](bool on){
        if(buildWallButton_)buildWallButton_->setText(on?"CANCEL WALL PLACEMENT":"BUILD STONE WALL · 12 Stone");
        if(hudPad_)hudPad_->setText(on?"Wall mode: left-click a tile within 5 tiles of your vehicle. Right-click/Esc cancels.":
            "Right-click a building or platform to select it.");
    };
    canvas_->onSelection=[this](const QString &type,int index){
        session_.selectObject(type,index);
        if(hudPad_)hudPad_->setText(QString("Selected: %1 %2").arg(type).arg(index>=0?QString::number(index+1):QString()));
    };
    auto *side=new QWidget;side->setFixedWidth(285);auto *sidebar=new QVBoxLayout(side);sidebar->setContentsMargins(8,7,8,7);
    layout->addWidget(side);
    sidebar->addWidget(heading("BATTLE HUD"));
    hudName_=new QLabel("Commander");sidebar->addWidget(hudName_);
    hudTeam_=new QLabel("Team —");sidebar->addWidget(hudTeam_);
    hudHealth_=new QLabel("Tank health —");sidebar->addWidget(hudHealth_);
    hudBases_=new QLabel("Base health —");sidebar->addWidget(hudBases_);
    respawnGroup_=new QGroupBox("CHOOSE YOUR NEXT VEHICLE");sidebar->addWidget(respawnGroup_);
    auto *respawnLayout=new QVBoxLayout(respawnGroup_);
    respawnHint_=new QLabel("Choose a vehicle, then confirm your respawn.");respawnHint_->setWordWrap(true);respawnLayout->addWidget(respawnHint_);
    respawnVehicle_=new QComboBox;respawnVehicle_->addItems({"Jeep", "Tank", "Missile launcher"});respawnLayout->addWidget(respawnVehicle_);
    respawnConfirm_=button("CONFIRM RESPAWN VEHICLE");respawnLayout->addWidget(respawnConfirm_);
    connect(respawnConfirm_,&QPushButton::clicked,this,[this]{session_.chooseRespawn(respawnVehicle_->currentText());canvas_->setFocus();});
    respawnGroup_->setVisible(false);
    auto *bank=new QGroupBox("TEAM RESOURCES");sidebar->addWidget(bank);
    auto *bankLayout=new QFormLayout(bank);
    static const QStringList names={"Oil","Gold","Iron","Copper","Wood","Stone","Tech"};
    for(const auto &name:names){auto *value=new QLabel("—");resourceLabels_.append(value);bankLayout->addRow(name,value);}
    playerSelect_=button("SELECT MY VEHICLE / UPGRADES");sidebar->addWidget(playerSelect_);
    buildWallButton_=button("BUILD STONE WALL · 12 Stone");sidebar->addWidget(buildWallButton_);
    connect(buildWallButton_,&QPushButton::clicked,this,[this]{canvas_->setWallMode(!canvas_->wallMode());canvas_->setFocus();});
    connect(playerSelect_,&QPushButton::clicked,this,[this]{canvas_->selectPlayer();canvas_->setFocus();});
    upgradeGroup_=new QGroupBox("SELECTED OBJECT · UPGRADES");sidebar->addWidget(upgradeGroup_,1);
    auto *upOuter=new QVBoxLayout(upgradeGroup_);upOuter->setContentsMargins(5,9,5,5);
    auto *upgradeScroll=new QScrollArea;upgradeScroll->setWidgetResizable(true);upgradeScroll->setFrameShape(QFrame::NoFrame);
    upOuter->addWidget(upgradeScroll);
    auto *inner=new QWidget;upgradeLayout_=new QVBoxLayout(inner);upgradeLayout_->setContentsMargins(4,4,4,4);
    upgradeLayout_->addWidget(new QLabel("Choose your tank, base or a tower platform."));upgradeLayout_->addStretch();
    upgradeScroll->setWidget(inner);
    hudPad_=new QLabel("Right-click vehicle/base/node/tower pad to select.");hudPad_->setWordWrap(true);sidebar->addWidget(hudPad_);
    hudResult_=new QLabel("Capture platforms to earn resources.");hudResult_->setWordWrap(true);sidebar->addWidget(hudResult_);
    sidebar->addStretch();
    auto *help=new QLabel("<b>CONTROLS</b>\nZQSD / arrows: move (AZERTY)\nMouse: aim | Left click / Space: shoot\nRight-click: select building/platform\nMouse wheel: zoom\nClick buildings to upgrade\nM = Jeep mine (3 Oil + 2 Iron + 2 Tech)\nB = wall mode, left-click tile to build\nWall costs 12 Stone, within 5 tiles\nRespawn: choose vehicle, 10 sec, Oil + Iron\nOn death: last tank upgrade is lost");
    help->setWordWrap(true);sidebar->addWidget(help);
    auto *preferencesButton=button("KEYBINDINGS & AUDIO");sidebar->addWidget(preferencesButton);
    connect(preferencesButton,&QPushButton::clicked,this,&MainWindow::preferences);
    auto *leaveButton=button("EXIT MATCH");sidebar->addWidget(leaveButton);
    connect(leaveButton,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,"Leave match","Disconnect and return to the main menu?")!=QMessageBox::Yes)return;
        session_.leave();updateUi();
    });
}
void MainWindow::preferences(){
    QDialog dialog(this);dialog.setWindowTitle("TankWars | Controls & Sound");dialog.setMinimumWidth(420);
    auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(heading("CONTROLS & AUDIO"));
    layout->addWidget(new QLabel("Click a key field, then press the desired key. AZERTY is the default."));
    auto *form=new QFormLayout;layout->addLayout(form);
    std::array<int,7> codes={controls_.up,controls_.left,controls_.down,controls_.right,controls_.fire,controls_.mine,controls_.wall};
    const QStringList captions={"Move up","Move left","Move down","Move right","Fire","Lay mine (Jeep)","Build stone wall"};
    std::array<QKeySequenceEdit*,7> fields{};
    for(int k=0;k<7;++k){
        fields[k]=new QKeySequenceEdit(QKeySequence(codes[k]),&dialog);
        form->addRow(captions[k],fields[k]);
    }
    auto *defaults=button("RESTORE AZERTY (ZQSD)");layout->addWidget(defaults);
    connect(defaults,&QPushButton::clicked,&dialog,[&]{
        const std::array<int,7> keys={Qt::Key_Z,Qt::Key_Q,Qt::Key_S,Qt::Key_D,Qt::Key_Space,Qt::Key_M,Qt::Key_B};
        for(int k=0;k<7;++k)fields[k]->setKeySequence(QKeySequence(keys[k]));
    });
    auto *soundOn=new QCheckBox("Enable sound effects",&dialog);soundOn->setChecked(sounds_.enabled());
    soundOn->setEnabled(sounds_.available());layout->addWidget(soundOn);
    auto *volume=new QSlider(Qt::Horizontal,&dialog);volume->setRange(0,100);
    volume->setValue(sounds_.volume());volume->setEnabled(sounds_.volumeControlAvailable());
    form->addRow("Sound volume",volume);
    auto *buttons=new QHBoxLayout;layout->addLayout(buttons);
    auto *cancel=button("CANCEL"),*apply=button("APPLY & SAVE");apply->setObjectName("primary");
    buttons->addWidget(cancel);buttons->addWidget(apply);
    connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    connect(apply,&QPushButton::clicked,&dialog,[&]{
        std::array<int,7> keys{};QSet<int> used;
        for(int k=0;k<7;++k){
            auto sequence=fields[k]->keySequence();
            if(sequence.count()!=1||sequence[0].keyboardModifiers()!=Qt::NoModifier||
               sequence[0].key()==Qt::Key_unknown){
                QMessageBox::warning(&dialog,"Invalid binding","Select exactly one unmodified key for every action.");return;
            }
            keys[k]=int(sequence[0].key());
            if(used.contains(keys[k])){
                QMessageBox::warning(&dialog,"Duplicate binding","Assign a different key to each action.");return;
            }
            used.insert(keys[k]);
        }
        controls_={keys[0],keys[1],keys[2],keys[3],keys[4],keys[5],keys[6]};
        controls_.save();canvas_->setControls(controls_);
        if(sounds_.available()){sounds_.setEnabled(soundOn->isChecked());sounds_.setVolume(volume->value());sounds_.play("click");}
        dialog.accept();
    });
    dialog.exec();
    if(session_.phase()==Session::Phase::Playing)canvas_->setFocus();
}
void MainWindow::host(){
    if(name_->text().trimmed().isEmpty()){QMessageBox::warning(this,"Player name","Enter a player name.");return;}
    if(!session_.create(quint16(port_->value()),name_->text())){
        QMessageBox::warning(this,"Hosting failed",session_.failure());return;
    }
    session_.useMap(pendingMap_);statusBar()->showMessage("Hosting TCP port "+QString::number(port_->value()));updateUi();
}
void MainWindow::join(){
    if(name_->text().trimmed().isEmpty()||ip_->text().trimmed().isEmpty()){
        QMessageBox::warning(this,"Join","Enter a nickname and host IP.");return;
    }
    session_.join(ip_->text().trimmed(),quint16(port_->value()),name_->text());
    statusBar()->showMessage("Connecting to "+ip_->text()+"...");
}
void MainWindow::openEditor(bool inLobby){
    MapEditor editor(this);editor.setMap(inLobby?session_.map():pendingMap_);
    if(editor.exec()!=QDialog::Accepted)return;
    pendingMap_=editor.edited();if(inLobby&&session_.isHost())session_.useMap(pendingMap_);
    statusBar()->showMessage("Map selected: "+pendingMap_.name);
}
void MainWindow::selectMap(){
    QString path=QFileDialog::getOpenFileName(this,"Select TankWars map",mapDirectory(),"TankWars maps (*.json)");
    if(path.isEmpty())return;
    ArenaMap map;QString error;
    if(!loadMap(path,map,&error)||!map.validate(session_.options().teams,&error)){
        QMessageBox::warning(this,"Map invalid",error);return;
    }
    pendingMap_=map;session_.useMap(map);
}
void MainWindow::setOptions(){
    if(!session_.isHost()||session_.phase()!=Session::Phase::Lobby)return;
    MatchOptions o;o.teams=teams_->currentIndex()==1?4:2;
    o.captureSeconds=capture_->value();o.supplyMultiplier=supply_->value();
    o.respawnCost=respawn_->value();o.friendlyFire=friendly_->isChecked();session_.configure(o);
}
void MainWindow::updateLobby(){
    bool host=session_.isHost();int count=session_.players().size();
    lobbyHeader_->setText(host?"HOST LOBBY  ·  YOU CONTROL THE MATCH":"JOINED LOBBY  ·  WAITING FOR HOST");
    mapLabel_->setText(QString("Map: %1    |    Connected slots: %2 / 8    |    TCP port: %3")
        .arg(session_.map().name).arg(count).arg(port_->value()));
    const auto &o=session_.options();
    {QSignalBlocker a(teams_),b(capture_),c(supply_),d(respawn_),e(friendly_);
        teams_->setCurrentIndex(o.teams==4?1:0);capture_->setValue(o.captureSeconds);
        supply_->setValue(o.supplyMultiplier);respawn_->setValue(o.respawnCost);friendly_->setChecked(o.friendlyFire);}
    QVector<QWidget*> settingsWidgets={teams_,capture_,supply_,respawn_,friendly_,addBot_,removeBot_,openMap_,editMap_,botType_};
    for(QWidget *widget:settingsWidgets)widget->setEnabled(host);
    addBot_->setEnabled(host&&count<8);
    lobbyPlayers_->setRowCount(count);
    bool allReady=count>0;QSet<int> activeTeams;
    for(int row=0;row<count;++row){const auto &p=session_.players().at(row);
        activeTeams.insert(p.team);
        if(!p.bot&&!p.ready)allReady=false;
        QStringList cells={p.name,QString("Team %1").arg(p.team+1),p.bot?"AI · "+p.strategy+" · "+p.vehicle:"Human · "+p.vehicle,
                           p.ready?"READY":"NOT READY",p.status};
        for(int c=0;c<cells.size();++c){auto *item=new QTableWidgetItem(cells[c]);
            if(c==1)item->setForeground(session_.teamColors().value(p.team));
            lobbyPlayers_->setItem(row,c,item);
        }
    }
    start_->setVisible(host);start_->setEnabled(host&&allReady&&activeTeams.size()==o.teams);
    bool myReady=false;int myTeam=0;QString myVehicle="Tank";
    for(const auto &p:session_.players())if(p.id==session_.myId()){myReady=p.ready;myTeam=p.team;myVehicle=p.vehicle;break;}
    ready_->setText(myReady?"CANCEL READY":"I'M READY");
    {QSignalBlocker blocker(teamChoice_);teamChoice_->clear();
        for(int t=0;t<o.teams;++t)teamChoice_->addItem("Team "+QString::number(t+1));
        teamChoice_->setCurrentIndex(myTeam);
    }
    {QSignalBlocker blocker(vehicleChoice_);vehicleChoice_->setCurrentText(myVehicle);}
}
void MainWindow::updateLoading(){
    auto phase=session_.phase();
    if(phase==Session::Phase::Countdown){
        loadingHeader_->setText(QString("MATCH STARTING IN %1").arg(session_.countdown()));
        loadingHint_->setText("All players are ready. Preparing map synchronization...");
    }else{
        loadingHeader_->setText("LOADING MAPS & PLAYERS");
        QStringList waiting=session_.waitingFor();
        loadingHint_->setText(waiting.isEmpty()?"All players loaded. Waiting for host confirmation...":
            "Waiting for:\n"+waiting.join("\n"));
    }
    loadingPlayers_->setRowCount(session_.players().size());
    int row=0;
    for(const auto &p:session_.players()){
        loadingPlayers_->setItem(row,0,new QTableWidgetItem(p.name));
        loadingPlayers_->setItem(row,1,new QTableWidgetItem(p.status));
        auto *progress=new QProgressBar;progress->setRange(0,100);progress->setValue(p.progress);
        loadingPlayers_->setCellWidget(row,2,progress);
        loadingPlayers_->setItem(row,3,new QTableWidgetItem(p.loaded?"READY":"WAITING"));
        ++row;
    }
}
void MainWindow::startCanvas(){
    lastFrame_={};upgradeSignature_.clear();canvas_->setMap(session_.map());canvas_->setLocalId(session_.myId());
    canvas_->setColors(session_.teamColors());canvas_->setSnapshot({});
    if(session_.phase()==Session::Phase::Loading)session_.loaded();
}
void MainWindow::updateHud(const QJsonObject &snap){
    int team=-1;QJsonObject mine;
    for(const auto &j:snap.value("players").toArray())if(j.toObject().value("id").toInt()==session_.myId()){
        mine=j.toObject();team=mine.value("team").toInt();break;
    }
    if(team<0)return;
    hudName_->setText(mine.value("name").toString()+"  ·  "+
        QString("%1 (%2 armor) · A%3 G%4 S%5 R%6 F%7")
            .arg(mine.value("vehicle").toString()).arg(mine.value("armorClass").toString())
            .arg(mine.value("armor").toInt()).arg(mine.value("gun").toInt()).arg(mine.value("engine").toInt())
            .arg(mine.value("radar").toInt()).arg(mine.value("fire_rate").toInt()));
    const bool dead=!mine.value("alive").toBool();
    respawnGroup_->setVisible(dead);
    upgradeGroup_->setVisible(!dead);playerSelect_->setEnabled(!dead);buildWallButton_->setEnabled(!dead);
    if(dead&&canvas_->wallMode())canvas_->setWallMode(false);
    if(dead){
        const QString choice=mine.value("respawnVehicle").toString();
        int remaining=std::max(0,int(std::ceil(mine.value("respawn").toDouble())));
        const int cost=session_.options().respawnCost;
        respawnHint_->setText(choice.isEmpty()?
            QString("Choose Jeep / Tank / Missile launcher, then confirm.\nRespawn in %1s, costs %2 Oil + %2 Iron. No automatic vehicle selected.").arg(remaining).arg(cost):
            QString("Confirmed: %1. %2s remaining. Costs %3 Oil + %3 Iron. Spawns when resources are available.").arg(choice).arg(remaining).arg(cost));
        respawnConfirm_->setText(choice.isEmpty()?"CONFIRM RESPAWN VEHICLE":"CHANGE RESPAWN SELECTION");
    }
    hudTeam_->setText(QString("TEAM %1").arg(team+1));
    hudTeam_->setStyleSheet(QString("font-weight:bold;color:%1;").arg(session_.teamColors().value(team).name()));
    if(mine.value("alive").toBool())hudHealth_->setText(QString("Vehicle: %1 / %2 HP")
        .arg(int(mine.value("hp").toDouble())).arg(mine.value("maxHp").toInt()));
    else hudHealth_->setText(QString("DESTROYED · %1s · select a respawn vehicle")
        .arg(std::max(0,int(std::ceil(mine.value("respawn").toDouble())))));
    QJsonObject base;
    for(const auto &entry:snap.value("bases").toArray()){
        const auto item=entry.toObject();if(item.value("team").toInt(-1)==team){base=item;break;}
    }
    hudBases_->setText(base.isEmpty()?"Base: unavailable":QString("Base: %1 / %2 HP")
        .arg(int(base.value("hp").toDouble())).arg(int(base.value("maxHp").toDouble())));
    auto banks=snap.value("banks").toArray();
    if(team<banks.size()){auto resources=banks.at(team).toArray();
        for(int t=0;t<resourceLabels_.size()&&t<resources.size();++t)resourceLabels_[t]->setText(QString::number(resources.at(t).toInt()));
    }
    if(snap.value("winner").toInt(-1)>=0)hudResult_->setText(QString("MATCH OVER — Team %1 wins!").arg(snap.value("winner").toInt()+1));
    else {const QString feedback=snap.value("feedback").toString();
        hudResult_->setText(feedback.isEmpty()?"Capture nodes and tower pads. Your base produces Oil + Iron.":feedback);}
}
void MainWindow::updateUpgrades(const QJsonObject &snapshot){
    if(!upgradeLayout_)return;
    const QJsonArray offers=snapshot.value("offers").toArray();
    const QString selection=snapshot.value("selection").toString("player");
    int index=snapshot.value("selectionIndex").toInt(-1);
    const QByteArray signature=QJsonDocument(QJsonObject{{"selection",selection},{"index",index},{"offers",offers}}).toJson(QJsonDocument::Compact);
    if(signature==upgradeSignature_)return;
    upgradeSignature_=signature;
    while(auto *item=upgradeLayout_->takeAt(0)){
        if(item->widget())item->widget()->deleteLater();delete item;
    }
    auto *selectionLabel=new QLabel(QString("%1 %2").arg(selection.toUpper()).arg(index>=0?QString("#%1").arg(index+1):QString()));
    selectionLabel->setStyleSheet("font-weight:bold;color:#ffe2a2;");upgradeLayout_->addWidget(selectionLabel);
    if(offers.isEmpty()){
        auto *hint=new QLabel(selection=="node"?"Resource nodes cannot be upgraded. Control them to collect resources.":"Select a friendly structure or your vehicle.");
        hint->setWordWrap(true);upgradeLayout_->addWidget(hint);
    }
    for(const auto &entry:offers){
        auto offer=entry.toObject();QString costText;
        const auto cost=offer.value("cost").toArray();
        for(int r=0;r<cost.size()&&r<resourceNames.size();++r){int amount=cost[r].toInt();
            if(amount>0){if(!costText.isEmpty())costText+="  ·  ";costText+=QString("%1 %2").arg(amount).arg(resourceNames[r]);}
        }
        auto *buy=button(offer.value("label").toString());buy->setEnabled(offer.value("allowed").toBool());
        buy->setToolTip(offer.value("reason").toString());upgradeLayout_->addWidget(buy);
        const QString labelText=offer.value("allowed").toBool()?"AVAILABLE": "NOT AVAILABLE";
        auto *costLabel=new QLabel(QString("%1\n%2%3").arg(costText,labelText,
            offer.value("reason").toString().isEmpty()?QString():QString(" · %1").arg(offer.value("reason").toString())));
        costLabel->setWordWrap(true);costLabel->setStyleSheet(offer.value("allowed").toBool()?"color:#91d9b0;font-size:9pt;":"color:#f0a6a0;font-size:9pt;");
        upgradeLayout_->addWidget(costLabel);
        QString kind=offer.value("kind").toString();
        connect(buy,&QPushButton::clicked,this,[this,kind,selection,index]{
            session_.purchase(kind,selection=="pad"?index:selection=="base"?index:-1);
            sounds_.play("click");canvas_->setFocus();
        });
    }
    upgradeLayout_->addStretch();
}
void MainWindow::updateUi(){
    auto phase=session_.phase();
    QWidget *page=home_;
    if(phase==Session::Phase::Lobby){updateLobby();page=lobby_;}
    else if(phase==Session::Phase::Countdown||phase==Session::Phase::Loading){updateLoading();page=loading_;}
    else if(phase==Session::Phase::Playing||phase==Session::Phase::Ended){
        page=game_;canvas_->setColors(session_.teamColors());canvas_->setLocalId(session_.myId());
    }
    if(pages_->currentWidget()!=page){pages_->setCurrentWidget(page);
        if(page==game_)canvas_->setFocus();}
}
}
