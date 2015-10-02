/*
 * UFW KControl Module
 *
 * Copyright 2011 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "helper.h"
#include "config.h"
#include <QtCore/QDebug>
#include <QtCore/QByteArray>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTextCodec>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <sys/stat.h>

namespace UFW
{

#define FILE_PERMS        0644
#define DIR_PERMS         0755
#define KCM_UFW_DIR       "/etc/kcm_ufw"
#define PROFILE_EXTENSION ".ufw"
#define LOG_FILE          "/var/log/ufw.log"

static void setPermissions(const QString &f, int perms)
{
    //
    // Clear any umask before setting file perms
    mode_t oldMask(umask(0000));
    ::chmod(QFile::encodeName(f).constData(), perms);
    // Reset umask
    ::umask(oldMask);
}

static void checkFolder()
{
    QDir d(KCM_UFW_DIR);

    if(!d.exists())
    {
        d.mkpath(KCM_UFW_DIR);
        setPermissions(d.absolutePath(), DIR_PERMS);
    }
}

ActionReply Helper::query(const QVariantMap &args)
{
    qDebug() << __FUNCTION__;
    ActionReply reply=args["defaults"].toBool()
                        ? run(QStringList() << "--status" << "--defaults" << "--list" << "--modules", "query")
                        : run(QStringList() << "--status" << "--list", "query");

    if(args["profiles"].toBool()) {
        QDir dir(KCM_UFW_DIR);
        QStringList profiles=dir.entryList(QStringList() << "*"PROFILE_EXTENSION);
        QMap<QString, QVariant> data;
        foreach (const QString &profile, profiles) {
            QFile f(dir.canonicalPath()+QChar('/')+profile);
            if (f.open(QIODevice::ReadOnly)) {
                data.insert(profile, f.readAll());
            }
        }
        reply.addData("profiles", data);
    }

    return reply;
}

ActionReply Helper::viewlog(const QVariantMap &args)
{
    qDebug() << __FUNCTION__;

    QString     lastLine=args["lastLine"].toString(),
                logFile=args["logFile"].toString();
    QFile       file(logFile.isEmpty() ? QLatin1String(LOG_FILE) : logFile);
    ActionReply reply;

    if(file.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QStringList lines;
        while (!file.atEnd())
        {
            QString line(file.readLine());

            if(line.contains(" [UFW "))
            {
                if(!lastLine.isEmpty() && line==lastLine)
                {
                    lines.clear();
                    continue;
                }
                lines.append(line);
            }
        }

        reply.addData("lines", lines);
    }
    else
    {
        reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(STATUS_OPERATION_FAILED);
    }

    return reply;
}

ActionReply Helper::modify(const QVariantMap &args)
{
    qDebug() << __FUNCTION__;
    QString cmd=args["cmd"].toString();

    // QProcess converts its args using QString().toLocal8Bit()!!!, so use UTF-8 codec!!!
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    if("setStatus"==cmd)
        return setStatus(args, cmd);
    else if("addRules"==cmd)
        return addRules(args, cmd);
    else if("removeRule"==cmd)
        return removeRule(args, cmd);
    else if("moveRule"==cmd)
        return moveRule(args, cmd);
    else if("editRule"==cmd)
        return editRule(args, cmd);
    else if("editRuleDescr"==cmd)
        return editRuleDescr(args, cmd);
    else if("reset"==cmd)
        return reset(cmd);
    else if("setDefaults"==cmd)
        return setDefaults(args, cmd);
    else if("setModules"==cmd)
        return setModules(args, cmd);
    else if("setProfile"==cmd)
        return setProfile(args, cmd);
    else if("saveProfile"==cmd)
        return saveProfile(args, cmd);
    else if("deleteProfile"==cmd)
        return deleteProfile(args, cmd);

    ActionReply reply=ActionReply::HelperErrorReply;
    reply.setErrorCode(STATUS_INVALID_CMD);
    return reply;
}

ActionReply Helper::setStatus(const QVariantMap &args, const QString &cmd)
{
    return run(QStringList() << "--setEnabled="+QString(args["status"].toBool() ? "true" : "false"),
               QStringList() << "--status", cmd);
}

ActionReply Helper::setDefaults(const QVariantMap &args, const QString &cmd)
{
    QStringList query;

    query << "--defaults";
    if (args["ipv6"].toBool())
        query << "--list";
    return run(QStringList() << "--setDefaults="+args["xml"].toString(),
               query, cmd);
}

ActionReply Helper::setModules(const QVariantMap &args, const QString &cmd)
{
    return run(QStringList() << "--setModules="+args["xml"].toString(),
               QStringList() << "--modules", cmd);
}

ActionReply Helper::setProfile(const QVariantMap &args, const QString &cmd)
{
    QStringList cmdArgs;

    if(args.contains("ruleCount"))
    {
        unsigned int count=args["ruleCount"].toUInt();

        cmdArgs << "--clearRules";
        for(unsigned int i=0; i<count; ++i)
            cmdArgs << "--add="+args["rule"+QString().setNum(i)].toString();
    }

    if(args.contains("defaults"))
        cmdArgs << "--setDefaults="+args["defaults"].toString();
    if(args.contains("modules"))
        cmdArgs << "--setModules="+args["modules"].toString();

    if(cmdArgs.isEmpty())
    {
        ActionReply reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(STATUS_INVALID_ARGUMENTS);
        return reply;
    }
    else
    {
        checkFolder();
        return run(cmdArgs,
                   QStringList() << "--status" << "--defaults" << "--list" << "--modules", cmd);
    }
}

ActionReply Helper::saveProfile(const QVariantMap &args, const QString &cmd)
{
    qDebug() << __FUNCTION__ << args;

    QString     name(args["name"].toString()),
                xml(args["xml"].toString());
    ActionReply reply;

    if(name.isEmpty() || xml.isEmpty())
    {
        reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(STATUS_INVALID_ARGUMENTS);
    }
    else
    {
        checkFolder();

        QFile f(QString(KCM_UFW_DIR)+"/"+name+PROFILE_EXTENSION);

        if(f.open(QIODevice::WriteOnly))
        {
            QTextStream(&f) << xml;
            f.close();
            setPermissions(f.fileName(), FILE_PERMS);
        }
        else
        {
            reply=ActionReply::HelperErrorReply;
            reply.setErrorCode(STATUS_OPERATION_FAILED);
        }
    }

    reply.addData("cmd", cmd);
    reply.addData("name", name);
    reply.addData("profiles", QDir(KCM_UFW_DIR).entryList(QStringList() << "*"PROFILE_EXTENSION));
    return reply;
}

ActionReply Helper::deleteProfile(const QVariantMap &args, const QString &cmd)
{
    qDebug() << __FUNCTION__ << args;

    QString     name(args["name"].toString());
    ActionReply reply;

    if(name.isEmpty())
    {
        reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(STATUS_INVALID_ARGUMENTS);
    }
    else if(!QFile::remove(QString(KCM_UFW_DIR)+"/"+name+PROFILE_EXTENSION))
    {
        reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(STATUS_OPERATION_FAILED);
    }

    reply.addData("cmd", cmd);
    reply.addData("name", name);
    reply.addData("profiles", QDir(KCM_UFW_DIR).entryList(QStringList() << "*"PROFILE_EXTENSION));
    return reply;
}

ActionReply Helper::addRules(const QVariantMap &args, const QString &cmd)
{
    unsigned int count=args["count"].toUInt();

    if(count>0)
    {
        QStringList cmdArgs;

        for(unsigned int i=0; i<count; ++i)
            cmdArgs << "--add="+args["xml"+QString().setNum(i)].toString();

        checkFolder();
        return run(cmdArgs, QStringList() << "--list", cmd);
    }
    ActionReply reply=ActionReply::HelperErrorReply;
    reply.setErrorCode(STATUS_INVALID_ARGUMENTS);
    return reply;
}

ActionReply Helper::removeRule(const QVariantMap &args, const QString &cmd)
{
    checkFolder();
    return run(QStringList() << "--remove="+args["index"].toString(),
               QStringList() << "--list", cmd);
}

ActionReply Helper::moveRule(const QVariantMap &args, const QString &cmd)
{
    checkFolder();
    return run(QStringList() << "--move="+QString().setNum(args["from"].toUInt())+':'+
                                          QString().setNum(args["to"].toUInt()),
               QStringList() << "--list", cmd);
}

ActionReply Helper::editRule(const QVariantMap &args, const QString &cmd)
{
    checkFolder();
    return run(QStringList() << "--update="+args["xml"].toString(),
               QStringList() << "--list", cmd);
}

ActionReply Helper::editRuleDescr(const QVariantMap &args, const QString &cmd)
{
    checkFolder();
    return run(QStringList() << "--updateDescr="+args["xml"].toString(),
               QStringList() << "--list", cmd);
}

ActionReply Helper::reset(const QString &cmd)
{
    return run(QStringList() << "--reset",
               QStringList() << "--status" << "--defaults" << "--list" << "--modules", cmd);
}

ActionReply Helper::run(const QStringList &args, const QStringList &second, const QString &cmd)
{
    ActionReply reply=run(args, cmd);
    if(0==reply.errorCode())
        reply=run(second, cmd);
    return reply;
}

ActionReply Helper::run(const QStringList &args, const QString &cmd)
{
    QProcess    ufw;
    ActionReply reply;

    qDebug() << __FUNCTION__ << args;
    ufw.start(HELPER_DIR "/kcm_ufw_helper.py", args, QIODevice::ReadOnly);
    if (ufw.waitForStarted())
        ufw.waitForFinished();

    int exitCode(ufw.exitCode());

    if(0!=exitCode)
    {
        reply=ActionReply::HelperErrorReply;
        reply.setErrorCode(exitCode);
        reply.addData("response", ufw.readAllStandardError());
    }
    else
        reply.addData("response", ufw.readAllStandardOutput());
    reply.addData("cmd", cmd);
    return reply;
}

}

KDE4_AUTH_HELPER_MAIN("org.kde.ufw", UFW::Helper)
