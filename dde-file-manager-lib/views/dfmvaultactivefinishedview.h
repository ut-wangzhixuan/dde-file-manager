#ifndef DFMVAULTACTIVEFINISHEDVIEW_H
#define DFMVAULTACTIVEFINISHEDVIEW_H

#include <QWidget>
#include <dtkwidget_global.h>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QGridLayout;
QT_END_NAMESPACE

DWIDGET_BEGIN_NAMESPACE
class DIconButton;
class DWaterProgress;
DWIDGET_END_NAMESPACE

DWIDGET_USE_NAMESPACE

class DFMVaultActiveFinishedView : public QWidget
{
    Q_OBJECT
public:
    explicit DFMVaultActiveFinishedView(QWidget *parent = nullptr);

    void setFinishedBtnEnabled(bool b);

signals:
    void sigAccepted();

public slots:
    // 连接创建保险箱返回信号
    void slotEncryptComplete(int nState);

private slots:
    void slotEncryptVault();

private:
    QLabel              *m_pTips;
    QPushButton         *m_pFinishedBtn;
    QLabel              *m_pTips2;
    QLabel              *m_pTips3;

    DIconButton         *m_pEncryVaultImage;
    DWaterProgress      *m_pWaterProgress;

    QGridLayout         *play;
};

#endif // DFMVAULTACTIVEFINISHEDVIEW_H