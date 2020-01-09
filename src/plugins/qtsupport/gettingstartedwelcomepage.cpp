/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "gettingstartedwelcomepage.h"

#include "exampleslistmodel.h"
#include "screenshotcropper.h"

#include <utils/fileutils.h>
#include <utils/pathchooser.h>
#include <utils/theme/theme.h>
#include <utils/winutils.h>

#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/helpmanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/welcomepagehelper.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/project.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTimer>

using namespace Core;
using namespace Utils;

namespace QtSupport {
namespace Internal {

const char C_FALLBACK_ROOT[] = "ProjectsFallbackRoot";

ExamplesWelcomePage::ExamplesWelcomePage(bool showExamples)
    : m_showExamples(showExamples)
{
}

QString ExamplesWelcomePage::title() const
{
    return m_showExamples ? tr("Examples") : tr("Tutorials");
}

int ExamplesWelcomePage::priority() const
{
    return m_showExamples ? 30 : 40;
}

Id ExamplesWelcomePage::id() const
{
    return m_showExamples ? "Examples" : "Tutorials";
}

QString ExamplesWelcomePage::copyToAlternativeLocation(const QFileInfo& proFileInfo, QStringList &filesToOpen, const QStringList& dependencies)
{
    const QString projectDir = proFileInfo.canonicalPath();
    QDialog d(ICore::mainWindow());
    auto lay = new QGridLayout(&d);
    auto descrLbl = new QLabel;
    d.setWindowTitle(tr("Copy Project to writable Location?"));
    descrLbl->setTextFormat(Qt::RichText);
    descrLbl->setWordWrap(false);
    const QString nativeProjectDir = QDir::toNativeSeparators(projectDir);
    descrLbl->setText(QString::fromLatin1("<blockquote>%1</blockquote>").arg(nativeProjectDir));
    descrLbl->setMinimumWidth(descrLbl->sizeHint().width());
    descrLbl->setWordWrap(true);
    descrLbl->setText(tr("<p>The project you are about to open is located in the "
                         "write-protected location:</p><blockquote>%1</blockquote>"
                         "<p>Please select a writable location below and click \"Copy Project and Open\" "
                         "to open a modifiable copy of the project or click \"Keep Project and Open\" "
                         "to open the project in location.</p><p><b>Note:</b> You will not "
                         "be able to alter or compile your project in the current location.</p>")
                      .arg(nativeProjectDir));
    lay->addWidget(descrLbl, 0, 0, 1, 2);
    auto txt = new QLabel(tr("&Location:"));
    auto chooser = new PathChooser;
    txt->setBuddy(chooser);
    chooser->setExpectedKind(PathChooser::ExistingDirectory);
    chooser->setHistoryCompleter(QLatin1String("Qt.WritableExamplesDir.History"));
    QSettings *settings = ICore::settings();
    chooser->setPath(settings->value(QString::fromLatin1(C_FALLBACK_ROOT),
                                     DocumentManager::projectsDirectory().toString()).toString());
    lay->addWidget(txt, 1, 0);
    lay->addWidget(chooser, 1, 1);
    enum { Copy = QDialog::Accepted + 1, Keep = QDialog::Accepted + 2 };
    auto bb = new QDialogButtonBox;
    QPushButton *copyBtn = bb->addButton(tr("&Copy Project and Open"), QDialogButtonBox::AcceptRole);
    connect(copyBtn, &QAbstractButton::released, &d, [&d] { d.done(Copy); });
    copyBtn->setDefault(true);
    QPushButton *keepBtn = bb->addButton(tr("&Keep Project and Open"), QDialogButtonBox::RejectRole);
    connect(keepBtn, &QAbstractButton::released, &d, [&d] { d.done(Keep); });
    lay->addWidget(bb, 2, 0, 1, 2);
    connect(chooser, &PathChooser::validChanged, copyBtn, &QWidget::setEnabled);
    int code = d.exec();
    if (code == Copy) {
        QString exampleDirName = proFileInfo.dir().dirName();
        QString destBaseDir = chooser->path();
        settings->setValue(QString::fromLatin1(C_FALLBACK_ROOT), destBaseDir);
        QDir toDirWithExamplesDir(destBaseDir);
        if (toDirWithExamplesDir.cd(exampleDirName)) {
            toDirWithExamplesDir.cdUp(); // step out, just to not be in the way
            QMessageBox::warning(ICore::mainWindow(), tr("Cannot Use Location"),
                                 tr("The specified location already exists. "
                                    "Please specify a valid location."),
                                 QMessageBox::Ok, QMessageBox::NoButton);
            return QString();
        } else {
            QString error;
            QString targetDir = destBaseDir + QLatin1Char('/') + exampleDirName;
            if (FileUtils::copyRecursively(FilePath::fromString(projectDir),
                    FilePath::fromString(targetDir), &error)) {
                // set vars to new location
                const QStringList::Iterator end = filesToOpen.end();
                for (QStringList::Iterator it = filesToOpen.begin(); it != end; ++it)
                    it->replace(projectDir, targetDir);

                foreach (const QString &dependency, dependencies) {
                    const FilePath targetFile = FilePath::fromString(targetDir)
                            .pathAppended(QDir(dependency).dirName());
                    if (!FileUtils::copyRecursively(FilePath::fromString(dependency), targetFile,
                            &error)) {
                        QMessageBox::warning(ICore::mainWindow(), tr("Cannot Copy Project"), error);
                        // do not fail, just warn;
                    }
                }


                return targetDir + QLatin1Char('/') + proFileInfo.fileName();
            } else {
                QMessageBox::warning(ICore::mainWindow(), tr("Cannot Copy Project"), error);
            }

        }
    }
    if (code == Keep)
        return proFileInfo.absoluteFilePath();
    return QString();
}

void ExamplesWelcomePage::openProject(const ExampleItem &item)
{
    using namespace ProjectExplorer;
    QString proFile = item.projectPath;
    if (proFile.isEmpty())
        return;

    QStringList filesToOpen = item.filesToOpen;
    if (!item.mainFile.isEmpty()) {
        // ensure that the main file is opened on top (i.e. opened last)
        filesToOpen.removeAll(item.mainFile);
        filesToOpen.append(item.mainFile);
    }

    QFileInfo proFileInfo(proFile);
    if (!proFileInfo.exists())
        return;

    // If the Qt is a distro Qt on Linux, it will not be writable, hence compilation will fail
    // Same if it is installed in non-writable location for other reasons
    const bool needsCopy = withNtfsPermissions<bool>([proFileInfo] {
        QFileInfo pathInfo(proFileInfo.path());
        return !proFileInfo.isWritable()
                || !pathInfo.isWritable() /* path of .pro file */
                || !QFileInfo(pathInfo.path()).isWritable() /* shadow build directory */;
    });
    if (needsCopy)
        proFile = copyToAlternativeLocation(proFileInfo, filesToOpen, item.dependencies);

    // don't try to load help and files if loading the help request is being cancelled
    if (proFile.isEmpty())
        return;
    ProjectExplorerPlugin::OpenProjectResult result = ProjectExplorerPlugin::openProject(proFile);
    if (result) {
        ICore::openFiles(filesToOpen);
        ModeManager::activateMode(Core::Constants::MODE_EDIT);
        QUrl docUrl = QUrl::fromUserInput(item.docUrl);
        if (docUrl.isValid())
            HelpManager::showHelpUrl(docUrl, HelpManager::ExternalHelpAlways);
        ModeManager::activateMode(ProjectExplorer::Constants::MODE_SESSION);
    } else {
        ProjectExplorerPlugin::showOpenProjectError(result);
    }
}

//////////////////////////////

static QColor themeColor(Theme::Color role)
{
    return Utils::creatorTheme()->color(role);
}

static QFont sizedFont(int size, const QWidget *widget, bool underline = false)
{
    QFont f = widget->font();
    f.setPixelSize(size);
    f.setUnderline(underline);
    return f;
}

class ExampleDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const final
    {
        const ExampleItem item = index.data(ExamplesListModel::ExampleItemRole).value<ExampleItem>();
        const QRect rc = option.rect;

        // Quick hack for empty items in the last row.
        if (item.name.isEmpty())
            return;

        const int d = 10;
        const int x = rc.x() + d;
        const int y = rc.y() + d;
        const int w = rc.width() - 2 * d - GridProxyModel::GridItemGap;
        const int h = rc.height() - 2 * d;
        const bool hovered = option.state & QStyle::State_MouseOver;

        const int tagsBase = GridProxyModel::TagsSeparatorY + 10;
        const int shiftY = GridProxyModel::TagsSeparatorY - 20;
        const int nameY = GridProxyModel::TagsSeparatorY - 20;

        const QRect textRect = QRect(x, y + nameY, w, h);

        QTextOption wrapped;
        wrapped.setWrapMode(QTextOption::WordWrap);
        int offset = 0;
        if (hovered) {
            if (index != m_previousIndex) {
                m_previousIndex = index;
                m_startTime.start();
                m_currentArea = rc;
                m_currentWidget = qobject_cast<QAbstractItemView *>(
                    const_cast<QWidget *>(option.widget));
            }
            offset = m_startTime.elapsed() * GridProxyModel::GridItemHeight / 200; // Duration 200 ms.
            if (offset < shiftY)
                QTimer::singleShot(5, this, &ExampleDelegate::goon);
            else if (offset > shiftY)
                offset = shiftY;
        } else {
            m_previousIndex = QModelIndex();
        }

        const QFontMetrics fm(option.widget->font());
        const QRect shiftedTextRect = textRect.adjusted(0, -offset, 0, -offset);

        // The pixmap.
        if (offset == 0) {
            QPixmap pm = index.data(ExamplesListModel::ExampleImageRole).value<QPixmap>();
            QRect inner(x + 11, y - offset, ExamplesListModel::exampleImageSize.width(),
                        ExamplesListModel::exampleImageSize.height());
            QRect pixmapRect = inner;
            if (!pm.isNull()) {
                painter->setPen(foregroundColor2);
                if (!m_showExamples)
                    pixmapRect = inner.adjusted(6, 20, -6, -15);
                QPoint pixmapPos = pixmapRect.center();
                pixmapPos.rx() -= pm.width() / pm.devicePixelRatio() / 2;
                pixmapPos.ry() -= pm.height() / pm.devicePixelRatio() / 2;
                painter->drawPixmap(pixmapPos, pm);
                if (item.isVideo) {
                    painter->setFont(sizedFont(13, option.widget));
                    QString videoLen = item.videoLength;
                    painter->drawText(pixmapRect.adjusted(0, 0, 0, painter->font().pixelSize() + 3),
                                      videoLen, Qt::AlignBottom | Qt::AlignHCenter);
                }
            } else {
                // The description text as fallback.
                painter->setPen(foregroundColor2);
                painter->setFont(sizedFont(11, option.widget));
                painter->drawText(pixmapRect.adjusted(6, 10, -6, -10), item.description, wrapped);
            }
            painter->setPen(foregroundColor1);
            painter->drawRect(pixmapRect.adjusted(-1, -1, -1, -1));
        }

        // The title of the example.
        painter->setPen(foregroundColor1);
        painter->setFont(sizedFont(13, option.widget));
        QRectF nameRect;
        if (offset) {
            nameRect = painter->boundingRect(shiftedTextRect, item.name, wrapped);
            painter->drawText(nameRect, item.name, wrapped);
        } else {
            nameRect = QRect(x, y + nameY, x + w, y + nameY + 20);
            QString elidedName = fm.elidedText(item.name, Qt::ElideRight, w - 20);
            painter->drawText(nameRect, elidedName);
        }

        // The separator line below the example title.
        if (offset) {
            int ll = nameRect.bottom() + 5;
            painter->setPen(lightColor);
            painter->drawLine(x, ll, x + w, ll);
        }

        // The description text.
        if (offset) {
            int dd = nameRect.height() + 10;
            QRect descRect = shiftedTextRect.adjusted(0, dd, 0, dd);
            painter->setPen(foregroundColor2);
            painter->setFont(sizedFont(11, option.widget));
            painter->drawText(descRect, item.description, wrapped);
        }

        // Separator line between text and 'Tags:' section
        painter->setPen(lightColor);
        painter->drawLine(x, y + GridProxyModel::TagsSeparatorY,
                          x + w, y + GridProxyModel::TagsSeparatorY);

        // The 'Tags:' section
        const int tagsHeight = h - tagsBase;
        const QFont tagsFont = sizedFont(10, option.widget);
        const QFontMetrics tagsFontMetrics(tagsFont);
        QRect tagsLabelRect = QRect(x, y + tagsBase, 30, tagsHeight - 2);
        painter->setPen(foregroundColor2);
        painter->setFont(tagsFont);
        painter->drawText(tagsLabelRect, ExamplesWelcomePage::tr("Tags:"));

        painter->setPen(themeColor(Theme::Welcome_LinkColor));
        m_currentTagRects.clear();
        int xx = 0;
        int yy = y + tagsBase;
        for (const QString &tag : item.tags) {
            const int ww = tagsFontMetrics.horizontalAdvance(tag) + 5;
            if (xx + ww > w - 30) {
                yy += 15;
                xx = 0;
            }
            const QRect tagRect(xx + x + 30, yy, ww, 15);
            painter->drawText(tagRect, tag);
            m_currentTagRects.append({ tag, tagRect });
            xx += ww;
        }

        // Box it when hovered.
        if (hovered) {
            painter->setPen(lightColor);
            painter->drawRect(rc.adjusted(0, 0, -1, -1));
        }
    }

    void goon()
    {
        if (m_currentWidget)
            m_currentWidget->viewport()->update(m_currentArea);
    }

    bool editorEvent(QEvent *ev, QAbstractItemModel *model,
        const QStyleOptionViewItem &option, const QModelIndex &idx) final
    {
        if (ev->type() == QEvent::MouseButtonRelease) {
            const ExampleItem item = idx.data(Qt::UserRole).value<ExampleItem>();
            auto mev = static_cast<QMouseEvent *>(ev);
            if (idx.isValid()) {
                const QPoint pos = mev->pos();
                if (pos.y() > option.rect.y() + GridProxyModel::TagsSeparatorY) {
                    //const QStringList tags = idx.data(Tags).toStringList();
                    for (const auto &it : m_currentTagRects) {
                        if (it.second.contains(pos))
                            emit tagClicked(it.first);
                    }
                } else {
                    if (item.isVideo)
                        QDesktopServices::openUrl(QUrl::fromUserInput(item.videoUrl));
                    else if (item.hasSourceCode)
                        ExamplesWelcomePage::openProject(item);
                    else
                        HelpManager::showHelpUrl(QUrl::fromUserInput(item.docUrl),
                                                       HelpManager::ExternalHelpAlways);
                }
            }
        }
        return QStyledItemDelegate::editorEvent(ev, model, option, idx);
    }

    void setShowExamples(bool showExamples) { m_showExamples = showExamples; goon(); }

signals:
    void tagClicked(const QString &tag);

private:
    const QColor lightColor = QColor(221, 220, 220); // color: "#dddcdc"
    const QColor backgroundColor = themeColor(Theme::Welcome_BackgroundColor);
    const QColor foregroundColor1 = themeColor(Theme::Welcome_ForegroundPrimaryColor); // light-ish.
    const QColor foregroundColor2 = themeColor(Theme::Welcome_ForegroundSecondaryColor); // blacker.

    mutable QPersistentModelIndex m_previousIndex;
    mutable QElapsedTimer m_startTime;
    mutable QRect m_currentArea;
    mutable QPointer<QAbstractItemView> m_currentWidget;
    mutable QVector<QPair<QString, QRect>> m_currentTagRects;
    bool m_showExamples = true;
};

class ExamplesPageWidget : public QWidget
{
public:
    ExamplesPageWidget(bool isExamples)
        : m_isExamples(isExamples)
    {
        m_exampleDelegate.setShowExamples(isExamples);
        const int sideMargin = 27;
        static auto s_examplesModel = new ExamplesListModel(this);
        m_examplesModel = s_examplesModel;

        auto filteredModel = new ExamplesListModelFilter(m_examplesModel, !m_isExamples, this);

        auto searchBox = new SearchBox(this);
        m_searcher = searchBox->m_lineEdit;

        auto vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(30, sideMargin, 0, 0);

        auto hbox = new QHBoxLayout;
        if (m_isExamples) {
            m_searcher->setPlaceholderText(ExamplesWelcomePage::tr("Search in Examples..."));

            auto exampleSetSelector = new QComboBox(this);
            exampleSetSelector->setMinimumWidth(GridProxyModel::GridItemWidth);
            exampleSetSelector->setMaximumWidth(GridProxyModel::GridItemWidth);
            ExampleSetModel *exampleSetModel = m_examplesModel->exampleSetModel();
            exampleSetSelector->setModel(exampleSetModel);
            exampleSetSelector->setCurrentIndex(exampleSetModel->selectedExampleSet());
            connect(exampleSetSelector, QOverload<int>::of(&QComboBox::activated),
                    exampleSetModel, &ExampleSetModel::selectExampleSet);
            connect(exampleSetModel, &ExampleSetModel::selectedExampleSetChanged,
                    exampleSetSelector, &QComboBox::setCurrentIndex);

            hbox->setSpacing(17);
            hbox->addWidget(exampleSetSelector);
        } else {
            m_searcher->setPlaceholderText(ExamplesWelcomePage::tr("Search in Tutorials..."));
        }
        hbox->addWidget(searchBox);
        hbox->addSpacing(sideMargin);
        vbox->addItem(hbox);

        m_gridModel.setSourceModel(filteredModel);

        auto gridView = new GridView(this);
        gridView->setModel(&m_gridModel);
        gridView->setItemDelegate(&m_exampleDelegate);
        vbox->addWidget(gridView);

        connect(&m_exampleDelegate, &ExampleDelegate::tagClicked,
                this, &ExamplesPageWidget::onTagClicked);
        connect(m_searcher, &QLineEdit::textChanged,
                filteredModel, &ExamplesListModelFilter::setSearchString);
    }

    int bestColumnCount() const
    {
        return qMax(1, width() / (GridProxyModel::GridItemWidth + GridProxyModel::GridItemGap));
    }

    void resizeEvent(QResizeEvent *ev) final
    {
        QWidget::resizeEvent(ev);
        m_gridModel.setColumnCount(bestColumnCount());
    }

    void onTagClicked(const QString &tag)
    {
        QString text = m_searcher->text();
        m_searcher->setText(text + QString("tag:\"%1\" ").arg(tag));
    }

    const bool m_isExamples;
    ExampleDelegate m_exampleDelegate;
    QPointer<ExamplesListModel> m_examplesModel;
    QLineEdit *m_searcher;
    GridProxyModel m_gridModel;
};

QWidget *ExamplesWelcomePage::createWidget() const
{
    return new ExamplesPageWidget(m_showExamples);
}

} // namespace Internal
} // namespace QtSupport

#include "gettingstartedwelcomepage.moc"
