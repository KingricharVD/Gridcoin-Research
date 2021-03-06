

#include <QWidget>
#include <QListView>

#include "overviewpage.h"
#include "ui_overviewpage.h"

#ifndef Q_MOC_RUN
#include "main.h"
#endif
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"

#include <QAbstractItemDelegate>
#include <QPainter>

#ifdef WIN32
#include <QAxObject>
#include "../global_objects.hpp"
#include "../global_objects_noui.hpp"
#endif

#define DECORATION_SIZE 64

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(QObject *parent=nullptr): QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();

		QColor foreground = QColor(200, 0, 0);
        QVariant value = index.data(Qt::ForegroundRole);
        if(value.canConvert<QColor>())
        {
            foreground = qvariant_cast<QColor>(value);
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    currentBalance(-1),
    currentStake(0),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    txdelegate(new TxViewDelegate(this))
{
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    updateTransactions();

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    connect(ui->pollLabel, SIGNAL(clicked()), this, SLOT(handlePollLabelClicked()));

    // init "out of sync" warning labels
    ui->walletStatusLabel->setText("(" + tr("out of sync") + ")");
    ui->transactionsStatusLabel->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
	OverviewPage::UpdateBoincUtilization();

    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handlePollLabelClicked()
{
    emit pollLabelClicked();
}


void OverviewPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTransactions();
}

void OverviewPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTransactions();
}

void OverviewPage::updateTransactions()
{
    if(filter)
    {
        // Show the maximum number of transactions the transaction list widget
        // can hold without overflowing.
        const size_t itemHeight = DECORATION_SIZE + ui->listTransactions->spacing();
        const size_t contentsHeight = ui->listTransactions->height();
        const size_t numItems = contentsHeight / itemHeight;
        filter->setLimit(numItems);
        ui->listTransactions->update();
    }
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = model->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentStake = stake;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->balanceLabel->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->stakeLabel->setText(BitcoinUnits::formatWithUnit(unit, stake));
    ui->unconfirmedLabel->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->immatureLabel->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));
    ui->totalLabel->setText(BitcoinUnits::formatWithUnit(unit, balance + stake + unconfirmedBalance + immatureBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->immatureLabel->setVisible(showImmature);
    ui->immatureTextLabel->setVisible(showImmature);
	OverviewPage::UpdateBoincUtilization();

}

void OverviewPage::UpdateBoincUtilization()
{
    LOCK(GlobalStatusStruct.lock);
    ui->blocksLabel->setText(QString::fromUtf8(GlobalStatusStruct.blocks.c_str()));
    ui->difficultyLabel->setText(QString::fromUtf8(GlobalStatusStruct.difficulty.c_str()));
    ui->netWeightLabel->setText(QString::fromUtf8(GlobalStatusStruct.netWeight.c_str()));
    ui->coinWeightLabel->setText(QString::fromUtf8(GlobalStatusStruct.coinWeight.c_str()));
    ui->magnitudeLabel->setText(QString::fromUtf8(GlobalStatusStruct.magnitude.c_str()));
    ui->cpidLabel->setText(QString::fromUtf8(GlobalStatusStruct.cpid.c_str()));
    ui->statusLabel->setText(QString::fromUtf8(GlobalStatusStruct.status.c_str()));
    ui->pollLabel->setText(QString::fromUtf8(GlobalStatusStruct.poll.c_str()).replace(QChar('_'),QChar(' '), Qt::CaseSensitive));
    ui->errorsLabel->setText(QString::fromUtf8(GlobalStatusStruct.errors.c_str()));
}

void OverviewPage::setModel(WalletModel *model)
{
    this->model = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        UpdateBoincUtilization();
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, model->getStake(), currentUnconfirmedBalance, currentImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = model->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->walletStatusLabel->setVisible(fShow);
    ui->transactionsStatusLabel->setVisible(fShow);
	OverviewPage::UpdateBoincUtilization();
}

void OverviewPage::updateglobalstatus()
{

	OverviewPage::UpdateBoincUtilization();
}

