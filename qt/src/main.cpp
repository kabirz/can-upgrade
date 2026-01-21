#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QDebug>
#include <QIcon>
#include "mainwindow.h"

// 辅助函数：尝试加载翻译文件
bool loadTranslation(QApplication &app, QTranslator &translator, const QString &path)
{
    if (translator.load(path)) {
        app.installTranslator(&translator);
        qDebug() << "Loaded translation:" << path;
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 加载翻译文件
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    qDebug() << "System UI languages:" << uiLanguages;

    // 按优先级尝试加载翻译文件
    bool loaded = false;
    
    // 1. 尝试加载系统语言对应的翻译
    for (const QString &locale : uiLanguages) {
        QString path = QString(":/i18n/can_upgrade_%1.qm").arg(QLocale(locale).name());
        if (loadTranslation(app, translator, path)) {
            loaded = true;
            break;
        }
    }

    MainWindow window;

    window.show();
    return app.exec();
}
