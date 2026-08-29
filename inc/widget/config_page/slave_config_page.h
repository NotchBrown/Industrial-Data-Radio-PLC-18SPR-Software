#ifndef DRUPPC_SLAVE_CONFIG_PAGE_H
#define DRUPPC_SLAVE_CONFIG_PAGE_H

#include "config_page.h"

// Slave configuration: same tree as master, without the TX task table.
class SlaveConfigPage : public ConfigPage
{
    Q_OBJECT

public:
    explicit SlaveConfigPage(QWidget *parent = nullptr);
};

#endif // DRUPPC_SLAVE_CONFIG_PAGE_H
