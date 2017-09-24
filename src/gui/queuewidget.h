/**
 * Copyright (C) 2017 - Marcos Cardinot
 * @author Marcos Cardinot <mcardinot@gmail.com>
 */

#ifndef QUEUEWIDGET_H
#define QUEUEWIDGET_H

#include <QScrollArea>

#include "tablewidget.h"
#include "core/experimentsmgr.h"

class Ui_QueueWidget;

namespace evoplex {

class QueueWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit QueueWidget(ExperimentsMgr* expMgr, QWidget* parent = nullptr);

signals:
    void isEmpty(bool empty);

private slots:
    void slotStatusChanged(Experiment* exp);

private:
    enum Table {
        T_RUNNING,
        T_QUEUED,
        T_IDLE,
        T_INVALID
    };

    struct Row {
        QTableWidgetItem* item = nullptr;
        Table table = T_INVALID;
    };

    Ui_QueueWidget* m_ui;
    ExperimentsMgr* m_expMgr;

    QHash<QString, Row> m_rows; // map 'projId.expId' to the row
    QMap<TableWidget::Header, int> m_headerIdx; // map Header to column index

    QTableWidgetItem* insertRow(TableWidget *table, Experiment* exp);
};
}
#endif // QUEUEWIDGET_H
