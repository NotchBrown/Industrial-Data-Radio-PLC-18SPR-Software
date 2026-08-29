#ifndef DRUPPC_MASTER_CONFIG_PAGE_H
#define DRUPPC_MASTER_CONFIG_PAGE_H

#include "config_page.h"

// Master configuration: includes the TX task table group.
class MasterConfigPage : public ConfigPage
{
    Q_OBJECT

public:
    explicit MasterConfigPage(QWidget *parent = nullptr);
};

#endif // DRUPPC_MASTER_CONFIG_PAGE_H
