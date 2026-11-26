#pragma once
#include "community.h"
#include <QDialog>
QString chooseSample(QWidget *parent, Community &community, const QStringList &names);
struct Submission {
    QString name, author;
    bool accepted = false;
};
Submission confirmSubmission(QWidget *parent, bool capture, const QString &name, const QImage &preview,
                             bool customLogo);
void showHelp(QWidget *parent);
