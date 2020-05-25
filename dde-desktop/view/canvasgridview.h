/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#ifndef CANVASGRIDVIEW_H
#define CANVASGRIDVIEW_H

#include <QAbstractItemView>
#include <QScopedPointer>
#include <dfilemenumanager.h>
#include <QTime>

#define DesktopCanvasPath           "/com/deepin/dde/desktop/canvas"
#define DesktopCanvasInterface      "com.deepin.dde.desktop.Canvas"

enum DMD_TYPES : unsigned int;
class DUrl;
class DesktopItemDelegate;
class DFileSystemModel;
class DFileSelectionModel;
class CanvasViewPrivate;
class CanvasGridView: public QAbstractItemView
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", DesktopCanvasInterface)

    Q_PROPERTY(double dodgeDuration READ dodgeDuration WRITE setDodgeDuration NOTIFY dodgeDurationChanged)
public:
    static std::atomic<bool> m_flag;
    static QMap<DMD_TYPES, bool> virtualEntryExpandState;

    explicit CanvasGridView(const QString &screen,QWidget *parent = Q_NULLPTR);
    ~CanvasGridView() Q_DECL_OVERRIDE;


    enum ContextMenuAction {
        DisplaySettings = MenuAction::UserMenuAction + 1,
        CornerSettings,
        WallpaperSettings,

        FileManagerProperty,

        AutoMerge,
        AutoSort,

        IconSize,
        IconSize0 = IconSize,
        IconSize1 = IconSize + 1,
        IconSize2 = IconSize + 2,
        IconSize3 = IconSize + 3,
        IconSize4 = IconSize + 4,
    };
    Q_ENUM(ContextMenuAction)

    // inherint prue virtual function of QAbstractItemView
    virtual QRect visualRect(const QModelIndex &index) const Q_DECL_OVERRIDE;
    virtual QModelIndex indexAt(const QPoint &point) const Q_DECL_OVERRIDE;
    virtual void scrollTo(const QModelIndex &index,
                          ScrollHint hint = EnsureVisible) Q_DECL_OVERRIDE;
    virtual QModelIndex moveCursor(CursorAction cursorAction,
                                   Qt::KeyboardModifiers modifiers) Q_DECL_OVERRIDE;
    virtual int horizontalOffset() const Q_DECL_OVERRIDE;
    virtual int verticalOffset() const Q_DECL_OVERRIDE;
    virtual bool isIndexHidden(const QModelIndex &index) const Q_DECL_OVERRIDE;
    virtual void setSelection(const QRect &rect,
                              QItemSelectionModel::SelectionFlags command) Q_DECL_OVERRIDE;
    virtual QRegion visualRegionForSelection(const QItemSelection &selection) const Q_DECL_OVERRIDE;

    // event override
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void dragEnterEvent(QDragEnterEvent *event) Q_DECL_OVERRIDE;
    void dragMoveEvent(QDragMoveEvent *event) Q_DECL_OVERRIDE;
    void dragLeaveEvent(QDragLeaveEvent *event) Q_DECL_OVERRIDE;
    void dropEvent(QDropEvent *event) Q_DECL_OVERRIDE;
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE;
    //void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void focusInEvent(QFocusEvent *event) Q_DECL_OVERRIDE;
    void focusOutEvent(QFocusEvent *event) Q_DECL_OVERRIDE;
    void contextMenuEvent(QContextMenuEvent *event) Q_DECL_OVERRIDE;

    bool event(QEvent *event) override;

    virtual void rowsInserted(const QModelIndex &index, int first, int last) override;
    virtual void keyboardSearch(const QString &search) override;

#if QT_CONFIG(draganddrop)
    virtual void startDrag(Qt::DropActions supportedActions) override;
#endif

    void fakeDropEvent()noexcept;
    // list view function
    QString canvansScreenName();
    QRect rectForIndex(const QModelIndex &index) const;
    DUrl currentUrl() const;
    bool setCurrentUrl(const DUrl &url);
    void initRootUrl();
    bool setRootUrl(const DUrl &url);
    const DUrlList autoMergeSelectedUrls() const;
    const DUrlList selectedUrls() const;
    void setScreenNum(int num);
    bool isSelected(const QModelIndex &index) const;
    void select(const QList<DUrl> &list);
    int selectedIndexCount() const;

    DFileSystemModel *model() const;
    DFileSelectionModel *selectionModel() const;
    DesktopItemDelegate *itemDelegate() const;
    void setItemDelegate(DesktopItemDelegate *delegate);

    double dodgeDuration() const;

    void openUrl(const DUrl &url);
    void openUrls(const QList<DUrl> &urls);

    QMargins cellMargins() const;
    QSize cellSize() const;

    WId winId() const;
    void setAutoMerge(bool enabled = false);
    void toggleEntryExpandedState(const DUrl &url);
    void updateEntryExpandedState(const DUrl &url);
    void setGeometry(const QRect &rect);
    void delayModelRefresh(int ms = 50);
    DUrl currentCursorFile() const;
    inline int screenNum() const {return m_screenNum;}
    void syncIconLevel(int level);
    void updateHiddenItems();
signals:
    void sortRoleChanged(int role, Qt::SortOrder order);
    void autoAlignToggled();
    void autoMergeToggled();
    void changeIconLevel(int iconLevel);
    void dodgeDurationChanged(double dodgeDuration);

signals:
    void itemDeleted(const DUrl &url);
    void itemCreated(const DUrl &url);

public slots:
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) Q_DECL_OVERRIDE;
    void setDodgeDuration(double dragMoveTime);
    virtual void selectAll() override;
protected slots:
    void onRefreshFinished();
// Debug interface
public Q_SLOTS:
    Q_SCRIPTABLE void EnableUIDebug(bool enable);
    Q_SCRIPTABLE QString Size();
    Q_SCRIPTABLE QString Dump();
    Q_SCRIPTABLE QString DumpPos(qint32 x, qint32 y);
    Q_SCRIPTABLE void Refresh(); // 刷新桌面图标
protected:
    void delayAutoMerge(int ms = 50);
    void delayArrage(int ms = 50);
    void delayCustom(int ms = 50);
private:
    Q_DISABLE_COPY(CanvasGridView)

    friend class DEventFilter;

    void initUI();
    void initConnection();
    void updateCanvas();

    void setIconByLevel(int level);
    void increaseIcon();
    void decreaseIcon();

    inline QPoint gridAt(const QPoint &pos) const;
    inline QRect gridRectAt(const QPoint &pos) const;
    inline QList<QRect> itemPaintGeomertys(const QModelIndex &index) const;
    inline QRect itemIconGeomerty(const QModelIndex &index) const;

    inline QModelIndex firstIndex();
    inline QModelIndex lastIndex();

    void setSelection(const QRect &rect,
                      QItemSelectionModel::SelectionFlags command,
                      bool byIconRect);

    void handleContextMenuAction(int action);

    void showEmptyAreaMenu(const Qt::ItemFlags &indexFlags);
    void showNormalMenu(const QModelIndex &index, const Qt::ItemFlags &indexFlags);
    bool isIndexEmpty();
    QModelIndex moveCursorGrid(CursorAction cursorAction, Qt::KeyboardModifiers modifiers);

    void setGeometry(int, int, int, int) = delete;
    bool fetchDragEventUrlsFromSharedMemory();

    bool existScreensaverService();

    QScopedPointer<CanvasViewPrivate> d;
    double m_dragMoveTime;
    QList<QUrl> m_urlsForDragEvent;

    QString m_screenName;
    int     m_screenNum{1};

    QTimer *m_refreshTimer = nullptr;
    QTime m_rt; //刷新计时，可以删除
};


#endif // CANVASGRIDVIEW_H
