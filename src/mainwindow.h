#pragma once
#include "canvas.h"
#include "editor.h"
#include "session.h"
#include "sounds.h"
#include "controls.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QGroupBox>

namespace tw {
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent=nullptr);
private:
    Session session_;
    Controls controls_=Controls::load();
    SoundManager sounds_{this};
    ArenaMap pendingMap_=ArenaMap::standard();
    QStackedWidget *pages_=nullptr;
    QWidget *home_=nullptr,*lobby_=nullptr,*loading_=nullptr,*game_=nullptr;
    QLineEdit *name_=nullptr,*ip_=nullptr;
    QSpinBox *port_=nullptr,*capture_=nullptr,*supply_=nullptr,*respawn_=nullptr;
    QComboBox *teams_=nullptr,*teamChoice_=nullptr,*botType_=nullptr,*vehicleChoice_=nullptr;
    QCheckBox *friendly_=nullptr;
    QPushButton *start_=nullptr,*ready_=nullptr,*addBot_=nullptr,*removeBot_=nullptr,*openMap_=nullptr,*editMap_=nullptr;
    QLabel *lobbyHeader_=nullptr,*mapLabel_=nullptr,*loadingHeader_=nullptr,*loadingHint_=nullptr;
    QTableWidget *lobbyPlayers_=nullptr,*loadingPlayers_=nullptr;
    GameCanvas *canvas_=nullptr;
    QLabel *hudName_=nullptr,*hudHealth_=nullptr,*hudTeam_=nullptr,*hudBases_=nullptr,*hudPad_=nullptr,*hudResult_=nullptr;
    QGroupBox *upgradeGroup_=nullptr;QVBoxLayout *upgradeLayout_=nullptr;
    QPushButton *playerSelect_=nullptr;
    QVector<QLabel*> resourceLabels_;
    QJsonObject lastFrame_;
    QByteArray upgradeSignature_;
    int lastPage_=-1;
    void preferences();
    void constructHome();
    void constructLobby();
    void constructLoading();
    void constructGame();
    void host();
    void join();
    void openEditor(bool inLobby);
    void selectMap();
    void setOptions();
    void updateUi();
    void updateLobby();
    void updateLoading();
    void updateHud(const QJsonObject &snapshot);
    void startCanvas();
    void updateUpgrades(const QJsonObject &snapshot);
};
}
