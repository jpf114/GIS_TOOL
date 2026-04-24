#pragma once
#include <QObject>
#include <gis/framework/plugin.h>
#include <gis/framework/result.h>
#include <gis/framework/param_spec.h>
#include <map>
#include <memory>

class QtProgressReporter;

class ExecuteWorker : public QObject {
    Q_OBJECT
public:
    explicit ExecuteWorker(QObject* parent = nullptr);

    void setup(gis::framework::IGisPlugin* plugin,
               const std::map<std::string, gis::framework::ParamValue>& params,
               QtProgressReporter* reporter);

public slots:
    void run();

signals:
    void finished(const gis::framework::Result& result);

private:
    gis::framework::IGisPlugin* plugin_ = nullptr;
    std::map<std::string, gis::framework::ParamValue> params_;
    QtProgressReporter* reporter_ = nullptr;
};
