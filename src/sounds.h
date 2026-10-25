#pragma once
#include <QHash>
#include <QString>
#include <QObject>
#include <QSettings>
#ifdef TW_HAVE_MULTIMEDIA
#include <QSoundEffect>
#endif
namespace tw {
class SoundManager {
public:
    explicit SoundManager(QObject *parent);
    bool available()const;
    bool volumeControlAvailable()const;
    void play(const QString &name);
    void setEnabled(bool v){enabled_=v;QSettings().setValue("audio/enabled",v);}
    bool enabled()const{return enabled_;}
    void setVolume(int value);
    int volume()const{return volume_;}
private:
    bool enabled_=true;
    int volume_=65;
#ifdef TW_HAVE_MULTIMEDIA
    QHash<QString,QSoundEffect*> effects_;
#endif
#ifdef Q_OS_WIN
    QHash<QString,QString> wavFiles_;
#endif
};
}
