#include "cpcompute.h"
#include "lammpscontroller.h"
#include "mysimulator.h"
#include "../system.h"
#include <QDebug>
CPCompute::CPCompute(Qt3DCore::QNode *parent) : SimulatorControl(parent)
{

}

CPCompute::~CPCompute() { }

void CPCompute::setValues(double time, QVector<double> values)
{
    m_time = time;
    emit timeChanged(time);
    m_values.clear();
    m_values = QList<double>::fromVector(values);
    if(m_values.size()>0) {
       emit valuesChanged(m_values);
       emit firstValueChanged(m_values.at(0));
       if(values.size() > 1) secondValueChanged(m_values.at(1));
       if(values.size() > 2) thirdValueChanged(m_values.at(2));
       if(values.size() > 3) fourthValueChanged(m_values.at(3));
    }
}

void CPCompute::updateCommand()
{
    // For standard computes, command doesn't change
}

void CPCompute::update(LAMMPSController *lammpsController)
{
    SimulatorControl::update(lammpsController);
    LAMMPS_NS::Compute *lmp_compute = lammpsController->findComputeByIdentifier(identifier());
    if(lmp_compute != nullptr) {
       QVector<double> newValues;

       if(isVector()) {
           lmp_compute->compute_vector();
           double *values = lmp_compute->vector;
           int numValues = lmp_compute->size_vector;

           for(int i=0; i<numValues; i++) {
               newValues.push_back(values[i]);
           }

           if(m_dataSource) {

           }
       } else {
           double value = lmp_compute->compute_scalar();
           newValues.push_back(value);
       }

       setValues(lammpsController->system()->simulationTime(), newValues);
    }
}

QList<QString> CPCompute::enabledCommands()
{
    return { fullCommand() };
}

QList<QString> CPCompute::disableCommands()
{
    return {QString("uncompute %1").arg(identifier())};
}

bool CPCompute::existsInLammps(LAMMPSController *lammpsController)
{
    LAMMPS_NS::Compute *compute = lammpsController->findComputeByIdentifier(identifier());
    return compute!=nullptr;
}

QList<double> CPCompute::values() const
{
    return m_values;
}

double CPCompute::firstValue() const
{
    if(m_values.size()<1) return NAN;
    return m_values.at(0);
}

double CPCompute::secondValue() const
{
    if(m_values.size()<2) return NAN;
    return m_values.at(1);
}

double CPCompute::thirdValue() const
{
    if(m_values.size()<3) return NAN;
    return m_values.at(2);
}

double CPCompute::fourthValue() const
{
    if(m_values.size()<4) return NAN;
    return m_values.at(3);
}

double CPCompute::time() const
{
    return m_time;
}

bool CPCompute::isVector() const
{
    return m_isVector;
}

DataSource *CPCompute::dataSource() const
{
    return m_dataSource;
}

QString CPCompute::group() const
{
    return m_group;
}

void CPCompute::setIsVector(bool isVector)
{
    if (m_isVector == isVector)
        return;

    m_isVector = isVector;
    emit isVectorChanged(isVector);
}

void CPCompute::setDataSource(DataSource *dataSource)
{
    if (m_dataSource == dataSource)
        return;

    m_dataSource = dataSource;
    emit dataSourceChanged(dataSource);
}

void CPCompute::setGroup(QString group)
{
    if (m_group == group)
        return;

    m_group = group;
    emit groupChanged(group);
}


QList<QString> CPCompute::resetCommands()
{
    return { QString("uncompute %1").arg(identifier()), fullCommand() };
}

QString CPCompute::createCommandPrefix()
{
    return QString("compute %1 %2 ").arg(identifier()).arg(group());
}