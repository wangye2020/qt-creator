/****************************************************************************
**
** Copyright (C) 2019 Denis Shienkov <denis.shienkov@gmail.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "keilparser.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/task.h>

#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>

#include <QRegularExpression>

using namespace ProjectExplorer;

namespace BareMetal {
namespace Internal {

// Helpers:

static Task::TaskType taskType(const QString &msgType)
{
    if (msgType == "Warning" || msgType == "WARNING") {
        return Task::TaskType::Warning;
    } else if (msgType == "Error" || msgType == "ERROR"
               || msgType == "Fatal error" || msgType == "FATAL ERROR") {
        return Task::TaskType::Error;
    }
    return Task::TaskType::Unknown;
}

// KeilParser

KeilParser::KeilParser()
{
    setObjectName("KeilParser");
}

Core::Id KeilParser::id()
{
    return "BareMetal.OutputParser.Keil";
}

void KeilParser::newTask(const Task &task)
{
    doFlush();
    m_lastTask = task;
    m_lines = 1;
}

void KeilParser::amendDescription(const QString &desc)
{
    const int start = m_lastTask.description.count() + 1;
    m_lastTask.description.append(QLatin1Char('\n'));
    m_lastTask.description.append(desc);

    QTextLayout::FormatRange fr;
    fr.start = start;
    fr.length = m_lastTask.description.count() + 1;
    fr.format.setFont(TextEditor::TextEditorSettings::fontSettings().font());
    fr.format.setFontStyleHint(QFont::Monospace);
    m_lastTask.formats.append(fr);

    ++m_lines;
}

void KeilParser::stdError(const QString &line)
{
    IOutputParser::stdError(line);

    const QString lne = rightTrimmed(line);

    QRegularExpression re;
    QRegularExpressionMatch match;

    // ARM compiler specific patterns.

    re.setPattern("^\"(.+)\", line (\\d+).*:\\s+(Warning|Error):(\\s+|.+)([#|L].+)$");
    match = re.match(lne);
    if (match.hasMatch()) {
        enum CaptureIndex { FilePathIndex = 1, LineNumberIndex,
                            MessageTypeIndex, MessageNoteIndex, DescriptionIndex };
        const Utils::FileName fileName = Utils::FileName::fromUserInput(
                    match.captured(FilePathIndex));
        const int lineno = match.captured(LineNumberIndex).toInt();
        const Task::TaskType type = taskType(match.captured(MessageTypeIndex));
        const QString descr = match.captured(DescriptionIndex);
        const Task task(type, descr, fileName, lineno, Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
        return;
    }

    re.setPattern("^(Error|Fatal error):\\s(.+)$");
    match = re.match(lne);
    if (match.hasMatch()) {
        enum CaptureIndex { MessageTypeIndex = 1, DescriptionIndex };
        const Task::TaskType type = taskType(match.captured(MessageTypeIndex));
        const QString descr = match.captured(DescriptionIndex);
        const Task task(type, descr, {}, -1, Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
        return;
    }

    if (lne.startsWith(QLatin1Char(' '))) {
        amendDescription(lne);
        return;
    }

    doFlush();
}

void KeilParser::stdOutput(const QString &line)
{
    IOutputParser::stdOutput(line);

    const QString lne = rightTrimmed(line);

    QRegularExpression re;
    QRegularExpressionMatch match;

    // MSC51 compiler specific patterns.

    re.setPattern("^\\*{3} (WARNING|ERROR) (\\w+) IN LINE (\\d+) OF (.+\\.\\S+): (.+)$");
    match = re.match(lne);
    if (match.hasMatch()) {
        enum CaptureIndex { MessageTypeIndex = 1, MessageCodeIndex, LineNumberIndex,
                            FilePathIndex, MessageTextIndex };
        const Task::TaskType type = taskType(match.captured(MessageTypeIndex));
        const int lineno = match.captured(LineNumberIndex).toInt();
        const Utils::FileName fileName = Utils::FileName::fromUserInput(
                    match.captured(FilePathIndex));
        const QString descr = QString("%1: %2").arg(match.captured(MessageCodeIndex),
                                                    match.captured(MessageTextIndex));
        const Task task(type, descr, fileName, lineno, Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
    }

    re.setPattern("^\\*{3} (WARNING|ERROR) (#\\w+) IN (\\d+) \\((.+), LINE \\d+\\): (.+)$");
    match = re.match(lne);
    if (match.hasMatch()) {
        enum CaptureIndex { MessageTypeIndex = 1, MessageCodeIndex, LineNumberIndex,
                            FilePathIndex, MessageTextIndex };
        const Task::TaskType type = taskType(match.captured(MessageTypeIndex));
        const int lineno = match.captured(LineNumberIndex).toInt();
        const Utils::FileName fileName = Utils::FileName::fromUserInput(
                    match.captured(FilePathIndex));
        const QString descr = QString("%1: %2").arg(match.captured(MessageCodeIndex),
                                                    match.captured(MessageTextIndex));
        const Task task(type, descr, fileName, lineno, Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
    }

    re.setPattern("^\\*{3} (FATAL ERROR) (.+)$");
    match = re.match(lne);
    if (match.hasMatch()) {
        enum CaptureIndex { MessageTypeIndex = 1, MessageDescriptionIndex };
        const Task::TaskType type = taskType(match.captured(MessageTypeIndex));
        const QString descr = match.captured(MessageDescriptionIndex);
        const Task task(type, descr, {}, -1, Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
        return;
    }

    re.setPattern("^(A|C)51 FATAL[ |-]ERROR");
    match = re.match(lne);
    if (match.hasMatch()) {
        const QString key = match.captured(1);
        QString descr;
        if (key == QLatin1Char('A'))
            descr = "Assembler fatal error";
        else if (key == QLatin1Char('C'))
            descr = "Compiler fatal error";
        const Task task(Task::TaskType::Error, descr, {}, -1,
                        Constants::TASK_CATEGORY_COMPILE);
        newTask(task);
        return;
    }

    if (lne.startsWith(QLatin1Char(' '))) {
        amendDescription(lne);
        return;
    }

    doFlush();
}

void KeilParser::doFlush()
{
    if (m_lastTask.isNull())
        return;

    Task t = m_lastTask;
    m_lastTask.clear();
    emit addTask(t, m_lines, 1);
    m_lines = 0;
}

} // namespace Internal
} // namespace BareMetal

// Unit tests:

#ifdef WITH_TESTS
#include "baremetalplugin.h"
#include <projectexplorer/outputparser_test.h>
#include <QTest>

namespace BareMetal {
namespace Internal {

void BareMetalPlugin::testKeilOutputParsers_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<OutputParserTester::Channel>("inputChannel");
    QTest::addColumn<QString>("childStdOutLines");
    QTest::addColumn<QString>("childStdErrLines");
    QTest::addColumn<QList<ProjectExplorer::Task> >("tasks");
    QTest::addColumn<QString>("outputLines");

    QTest::newRow("pass-through stdout")
            << "Sometext" << OutputParserTester::STDOUT
            << "Sometext\n" << QString()
            << QList<Task>()
            << QString();
    QTest::newRow("pass-through stderr")
            << "Sometext" << OutputParserTester::STDERR
            << QString() << "Sometext\n"
            << QList<Task>()
            << QString();

    const Core::Id categoryCompile = Constants::TASK_CATEGORY_COMPILE;

    // ARM compiler specific patterns.

    QTest::newRow("ARM: No details warning")
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Warning: #1234: Some warning")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Warning: #1234: Some warning\n")
            << (QList<Task>() << Task(Task::Warning,
                                      QLatin1String("#1234: Some warning"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\main.c")),
                                      63,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: Details warning")
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Warning: #1234: Some warning\n"
                                   "      int f;\n"
                                   "          ^")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Warning: #1234: Some warning\n"
                                   "      int f;\n"
                                   "          ^\n")
            << (QList<Task>() << Task(Task::Warning,
                                      QLatin1String("#1234: Some warning\n"
                                                    "      int f;\n"
                                                    "          ^"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\main.c")),
                                      63,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: No details error")
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Error: #1234: Some error")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Error: #1234: Some error\n")
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("#1234: Some error"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\main.c")),
                                      63,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: No details error with column")
            << QString::fromLatin1("\"flash.sct\", line 51 (column 20): Error: L1234: Some error")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"flash.sct\", line 51 (column 20): Error: L1234: Some error\n")
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("L1234: Some error"),
                                      Utils::FileName::fromUserInput(QLatin1String("flash.sct")),
                                      51,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: Details error")
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Error: #1234: Some error\n"
                                   "      int f;\n"
                                   "          ^")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 63: Error: #1234: Some error\n"
                                   "      int f;\n"
                                   "          ^\n")
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("#1234: Some error\n"
                                                    "      int f;\n"
                                                    "          ^"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\main.c")),
                                      63,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: At end of source")
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 71: Error: At end of source:  #40: Some error")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("\"c:\\foo\\main.c\", line 71: Error: At end of source:  #40: Some error\n")
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("#40: Some error"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\main.c")),
                                      71,
                                      categoryCompile))
            << QString();

    QTest::newRow("ARM: Starts with error")
            << QString::fromLatin1("Error: L6226E: Some error.")
            << OutputParserTester::STDERR
            << QString()
            << QString::fromLatin1("Error: L6226E: Some error.\n")
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("L6226E: Some error."),
                                      Utils::FileName(),
                                      -1,
                                      categoryCompile))
            << QString();

    // MCS51 compiler specific patterns.

    // Assembler messages.
    QTest::newRow("MCS51: Assembler simple warning")
            << QString::fromLatin1("*** WARNING #A9 IN 15 (c:\\foo\\dscr.a51, LINE 15): Some warning")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** WARNING #A9 IN 15 (c:\\foo\\dscr.a51, LINE 15): Some warning\n")
            << QString()
            << (QList<Task>() << Task(Task::Warning,
                                      QLatin1String("#A9: Some warning"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\dscr.a51")),
                                      15,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Assembler simple error")
            << QString::fromLatin1("*** ERROR #A9 IN 15 (c:\\foo\\dscr.a51, LINE 15): Some error")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** ERROR #A9 IN 15 (c:\\foo\\dscr.a51, LINE 15): Some error\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("#A9: Some error"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo\\dscr.a51")),
                                      15,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Assembler fatal error")
            << QString::fromLatin1("A51 FATAL ERROR -\n"
                                   "  Some detail 1\n"
                                   "  Some detail N")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("A51 FATAL ERROR -\n"
                                   "  Some detail 1\n"
                                   "  Some detail N\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("Assembler fatal error\n"
                                                    "  Some detail 1\n"
                                                    "  Some detail N"),
                                      Utils::FileName(),
                                      -1,
                                      categoryCompile))
            << QString();

    // Compiler messages.
    QTest::newRow("MCS51: Compiler simple warning")
            << QString::fromLatin1("*** WARNING C123 IN LINE 13 OF c:\\foo.c: Some warning")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** WARNING C123 IN LINE 13 OF c:\\foo.c: Some warning\n")
            << QString()
            << (QList<Task>() << Task(Task::Warning,
                                      QLatin1String("C123: Some warning"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo.c")),
                                      13,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Compiler extended warning")
            << QString::fromLatin1("*** WARNING C123 IN LINE 13 OF c:\\foo.c: Some warning : 'extended text'")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** WARNING C123 IN LINE 13 OF c:\\foo.c: Some warning : 'extended text'\n")
            << QString()
            << (QList<Task>() << Task(Task::Warning,
                                      QLatin1String("C123: Some warning : 'extended text'"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo.c")),
                                      13,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Compiler simple error")
            << QString::fromLatin1("*** ERROR C123 IN LINE 13 OF c:\\foo.c: Some error")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** ERROR C123 IN LINE 13 OF c:\\foo.c: Some error\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("C123: Some error"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo.c")),
                                      13,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Compiler extended error")
            << QString::fromLatin1("*** ERROR C123 IN LINE 13 OF c:\\foo.c: Some error : 'extended text'")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** ERROR C123 IN LINE 13 OF c:\\foo.c: Some error : 'extended text'\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("C123: Some error : 'extended text'"),
                                      Utils::FileName::fromUserInput(QLatin1String("c:\\foo.c")),
                                      13,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Compiler fatal error")
            << QString::fromLatin1("C51 FATAL-ERROR -\n"
                                   "  Some detail 1\n"
                                   "  Some detail N")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("C51 FATAL-ERROR -\n"
                                   "  Some detail 1\n"
                                   "  Some detail N\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("Compiler fatal error\n"
                                                    "  Some detail 1\n"
                                                    "  Some detail N"),
                                      Utils::FileName(),
                                      -1,
                                      categoryCompile))
            << QString();

    // Linker messages.
    QTest::newRow("MCS51: Linker simple fatal error")
            << QString::fromLatin1("*** FATAL ERROR L456: Some error")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** FATAL ERROR L456: Some error\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("L456: Some error"),
                                      Utils::FileName(),
                                      -1,
                                      categoryCompile))
            << QString();

    QTest::newRow("MCS51: Linker extended fatal error")
            << QString::fromLatin1("*** FATAL ERROR L456: Some error\n"
                                   "    Some detail 1\n"
                                   "    Some detail N")
            << OutputParserTester::STDOUT
            << QString::fromLatin1("*** FATAL ERROR L456: Some error\n"
                                   "    Some detail 1\n"
                                   "    Some detail N\n")
            << QString()
            << (QList<Task>() << Task(Task::Error,
                                      QLatin1String("L456: Some error\n"
                                                    "    Some detail 1\n"
                                                    "    Some detail N"),
                                      Utils::FileName(),
                                      -1,
                                      categoryCompile))
            << QString();
}

void BareMetalPlugin::testKeilOutputParsers()
{
    OutputParserTester testbench;
    testbench.appendOutputParser(new KeilParser);
    QFETCH(QString, input);
    QFETCH(OutputParserTester::Channel, inputChannel);
    QFETCH(QList<Task>, tasks);
    QFETCH(QString, childStdOutLines);
    QFETCH(QString, childStdErrLines);
    QFETCH(QString, outputLines);

    testbench.testParsing(input, inputChannel,
                          tasks, childStdOutLines, childStdErrLines,
                          outputLines);
}

} // namespace Internal
} // namespace BareMetal

#endif // WITH_TESTS
