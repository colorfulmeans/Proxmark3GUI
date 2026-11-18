#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>

class AppPaths
{
public:
    static QString dataDirectory();
    static QString workspaceDirectory();
    static QString settingsFile();
    static bool initialize(QString* error = nullptr);
};

#endif
