#pragma once
#include <QSettings>
#include <Qt>

namespace tw {
struct Controls {
    int up=Qt::Key_Z,left=Qt::Key_Q,down=Qt::Key_S,right=Qt::Key_D,fire=Qt::Key_Space,mine=Qt::Key_M,wall=Qt::Key_B;
    static Controls load(){
        QSettings s;Controls c;
        c.up=s.value("controls/up",c.up).toInt();c.left=s.value("controls/left",c.left).toInt();
        c.down=s.value("controls/down",c.down).toInt();c.right=s.value("controls/right",c.right).toInt();
        c.fire=s.value("controls/fire",c.fire).toInt();
        c.mine=s.value("controls/mine",c.mine).toInt();c.wall=s.value("controls/wall",c.wall).toInt();return c;
    }
    void save() const {QSettings s;s.setValue("controls/up",up);s.setValue("controls/left",left);
        s.setValue("controls/down",down);s.setValue("controls/right",right);s.setValue("controls/fire",fire);
        s.setValue("controls/mine",mine);s.setValue("controls/wall",wall);}
};
}
