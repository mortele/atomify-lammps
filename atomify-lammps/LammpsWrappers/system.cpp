#include "system.h"
#include <domain.h>
#include <atom.h>
#include <output.h>
#include <thermo.h>
#include <update.h>
#include "atoms.h"
#include "regions.h"
#include "groups.h"
#include "computes.h"
#include "fixes.h"
#include "variables.h"
#include "../mysimulator.h"
#include "modifiers/modifier.h"
#include "units.h"
#include "../performance.h"

using namespace LAMMPS_NS;

System::System(AtomifySimulator *simulator)
{
    setAtoms(new Atoms(simulator));
    setGroups(new Groups(simulator));
    setRegions(new Regions(simulator));
    setComputes(new Computes(simulator));
    setUnits(new Units(simulator));
    setFixes(new Fixes(simulator));
    setVariables(new Variables(simulator));
    setPerformance(new Performance(simulator));
    m_transformationMatrix.setToIdentity();
}

void System::computeCellMatrix(Domain *domain) {
    domain->box_corners();
    QVector3D a(domain->corners[1][0], domain->corners[1][1], domain->corners[1][2]);
    QVector3D b(domain->corners[2][0], domain->corners[2][1], domain->corners[2][2]);
    QVector3D c(domain->corners[4][0], domain->corners[4][1], domain->corners[4][2]);
    QVector3D origo(domain->corners[0][0], domain->corners[0][1], domain->corners[0][2]);
    a -= origo;
    b -= origo;
    c -= origo;
    float values[] = {
        a[0], b[0], c[0],
        a[1], b[1], c[1],
        a[2], b[2], c[2]
    };

    setCellMatrix(QMatrix3x3(values));
}

void System::updateCenter(Domain *domain)
{
    QVector3D origo(domain->corners[0][0], domain->corners[0][1], domain->corners[0][2]);
    QVector3D end(domain->corners[7][0], domain->corners[7][1], domain->corners[7][2]);
    QVector3D newCenter = origo + 0.5*(end-origo);
    if( !(newCenter - m_center).isNull()) {
        m_center = newCenter;
        emit centerChanged(m_center);
    }
}

void System::updateTransformationMatrix(Domain *domain)
{
    double *h = domain->h;
    float transformationMatrixValues[] = { // Must cast for clang
        float(h[0]), float(h[5]), float(h[4]), 0,
        0,    float(h[1]), float(h[3]), 0,
        0,    0,    float(h[2]), 0,
        0,    0,    0,    0
    };

    m_transformationMatrix = QMatrix4x4(transformationMatrixValues);
    emit transformationMatrixChanged(m_transformationMatrix);
}

void System::updateBoundaryStyle(Domain *domain)
{
    QString style;
    for (int idim = 0; idim < domain->dimension; idim++) {
        if(domain->boundary[idim][0] == 0) {
            style += "p ";
            continue;
        } else {
            // Loop over the two sides and append only one letter if they are the same, append both if they differ
            for (int iside = 0; iside < 2; iside++) {
                QString dimStyle = "";
                if(domain->boundary[idim][iside] == 1) dimStyle = "f";
                else if(domain->boundary[idim][iside] == 2) dimStyle = "s";
                else if(domain->boundary[idim][iside] == 3) dimStyle = "m";

                if(iside == 0) {
                    style += dimStyle;
                    continue;
                } else if(domain->boundary[idim][0] != domain->boundary[idim][1]) {
                    style += dimStyle;
                }
                style += " ";
            }
        }
    }
    setBoundaryStyle(style);
}

void System::updateSizeAndOrigin(Domain *domain)
{
    bool originDidChange = false;
    bool sizeDidChange = false;
    for(int i=0; i<3; i++) {
        if( fabs(m_origin[i] - domain->boxlo[i]) > 1e-4) {
            m_origin[i] = domain->boxlo[i];
            originDidChange  = true;
        }
        if( fabs(m_size[i] - domain->prd[i]) > 1e-4) {
            m_size[i] = domain->prd[i];
            sizeDidChange = true;
        }
    }

    if(originDidChange) {
        emit originChanged(m_origin);
        emit centerChanged(center());
    }

    if(sizeDidChange) {
        emit sizeChanged(m_size);
        emit centerChanged(center());
    }
}

void System::calculateCPURemain(LAMMPS *lammps)
{
    double value;
    lammps->output->thermo->evaluate_keyword("cpuremain", &value);
    m_cpuremain = value;
}

void System::calculateTimestepsPerSeconds(LAMMPS *lammps)
{
    if(m_currentTimestep % 10 == 0) {
        double value;
        lammps->output->thermo->evaluate_keyword("spcpu", &value);
        if(value < 0) return;
        double oldValue = m_performance->timestepsPerSecond();
        value = 0.6*oldValue + 0.4*value; // low pass filter
        performance()->setTimestepsPerSecond(value);
    }
}

void System::synchronize(LAMMPSController *lammpsController)
{
    LAMMPS *lammps = lammpsController->lammps();
    if(!lammps) {
        reset();
        return;
    }
    setIsValid(true);

    m_regions->synchronize(lammpsController);
    m_groups->synchronize(lammpsController);
    m_atoms->synchronize(lammpsController);
    m_computes->synchronize(lammpsController);
    m_variables->synchronize(lammpsController);
    m_fixes->synchronize(lammpsController);
    m_performance->synchronize(lammpsController);

    Domain *domain = lammps->domain;
    Atom *atom = lammps->atom;
    Update *update = lammps->update;
    if(!domain || !atom || !update) return; // These may not be set in LAMMPS (they probably are, but an easy test).

    updateTransformationMatrix(domain);
    updateSizeAndOrigin(domain);
    computeCellMatrix(domain);
    updateBoundaryStyle(domain);
    setTriclinic(domain->triclinic);
    updateCenter(domain);

    if(m_numberOfAtoms != atom->natoms) {
        m_numberOfAtoms = atom->natoms;
        emit numberOfAtomsChanged(m_numberOfAtoms);
    }

    if(m_numberOfAtomTypes != atom->ntypes) {
        m_numberOfAtomTypes = atom->ntypes;
        emit numberOfAtomTypesChanged(m_numberOfAtomTypes);
    }

    float currentTime = update->atime + update->dt*(update->ntimestep - update->atimestep);
    if(m_simulationTime != currentTime) {
        m_simulationTime = currentTime;
        emit simulationTimeChanged(m_simulationTime);
    }

    if(m_currentTimestep != update->ntimestep) {
        m_currentTimestep = update->ntimestep;
        emit currentTimestepChanged(m_currentTimestep);
    }

    for(QVariant modifier_ : m_atoms->modifiers()) {
        Modifier *modifier = modifier_.value<Modifier*>();
        modifier->setSystem(this);
    }
    m_volume = m_size[0]*m_size[1]*m_size[2];
    emit volumeChanged(m_volume);

    setDt(lammps->update->dt);

//    lammps->output->thermo->compute(1);
//    calculateTimestepsPerSeconds(lammps);
//    calculateCPURemain(lammps);

    m_units->synchronize(lammps);
}

QVector3D System::origin() const
{
    return m_origin;
}

QVector3D System::size() const
{
    return m_size;
}

int System::numberOfAtoms() const
{
    return m_numberOfAtoms;
}

float System::simulationTime() const
{
    return m_simulationTime;
}

int System::currentTimestep() const
{
    return m_currentTimestep;
}

Atoms *System::atoms() const
{
    return m_atoms;
}

Regions *System::regions() const
{
    return m_regions;
}

Groups *System::groups() const
{
    return m_groups;
}

int System::numberOfAtomTypes() const
{
    return m_numberOfAtomTypes;
}

void System::reset()
{
    setIsValid(false);
    setTriclinic(false);
    m_fixes->reset();
    m_atoms->reset();
    m_groups->reset();
    m_computes->reset();
    m_regions->reset();
    m_variables->reset();
    m_units->reset();
    m_performance->reset();
    setDt(0);
    setCpuremain(0);
    m_currentTimestep = 0;
    m_simulationTime = 0;
    m_size = QVector3D();
    m_origin = QVector3D();
    m_numberOfAtoms = 0;
    m_numberOfAtomTypes = 0;
    m_center = QVector3D();
    m_volume = 0;
    setBoundaryStyle("None");
    emit currentTimestepChanged(m_currentTimestep);
    emit simulationTimeChanged(m_simulationTime);
    emit sizeChanged(m_size);
    emit centerChanged(m_center);
    emit originChanged(m_origin);
    emit numberOfAtomsChanged(m_numberOfAtoms);
    emit numberOfAtomTypesChanged(m_numberOfAtomTypes);
    emit volumeChanged(m_volume);
}

float System::volume() const
{
    return m_volume;
}

bool System::isValid() const
{
    return m_isValid;
}

QMatrix4x4 System::transformationMatrix() const
{
    return m_transformationMatrix;
}

QMatrix3x3 System::cellMatrix() const
{
    return m_cellMatrix;
}

QString System::boundaryStyle() const
{
    return m_boundaryStyle;
}

bool System::triclinic() const
{
    return m_triclinic;
}

Performance *System::performance() const
{
    return m_performance;
}

double System::cpuremain() const
{
    return m_cpuremain;
}

double System::dt() const
{
    return m_dt;
}

void System::synchronizeQML(LAMMPSController *lammpsController)
{
    m_computes->synchronizeQML(lammpsController);
    m_variables->synchronizeQML(lammpsController);
    m_fixes->synchronizeQML(lammpsController);
    m_regions->synchronizeQML(lammpsController);
    m_groups->synchronizeQML(lammpsController);
}

void System::updateThreadOnDataObjects(QThread *thread)
{
    m_computes->updateThreadOnDataObjects(thread);
}

QVector3D System::cameraPosition() const
{
    return m_cameraPosition;
}

QVector3D System::center() const
{
    return m_center;
}

Variables *System::variables() const
{
    return m_variables;
}

Units *System::units() const
{
    return m_units;
}

Fixes *System::fixes() const
{
    return m_fixes;
}

Computes *System::computes() const
{
    return m_computes;
}

void System::setAtoms(Atoms *atoms)
{
    if (m_atoms == atoms)
        return;

    m_atoms = atoms;
    emit atomsChanged(atoms);
}

void System::setRegions(Regions *regions)
{
    if (m_regions == regions)
        return;

    m_regions = regions;
    emit regionsChanged(regions);
}

void System::setGroups(Groups *groups)
{
    if (m_groups == groups)
        return;

    m_groups = groups;
    emit groupsChanged(groups);
}

void System::setIsValid(bool isValid)
{
    if (m_isValid == isValid)
        return;

    m_isValid = isValid;
    emit isValidChanged(isValid);
}

void System::setComputes(Computes *computes)
{
    if (m_computes == computes)
        return;

    m_computes = computes;
    emit computesChanged(computes);
}

void System::setCameraPosition(QVector3D cameraPosition)
{
    if (m_cameraPosition == cameraPosition)
        return;

    m_cameraPosition = cameraPosition;
    emit cameraPositionChanged(cameraPosition);
}

void System::setUnits(Units *units)
{
    if (m_units == units)
        return;

    m_units = units;
    emit unitsChanged(units);
}

void System::setFixes(Fixes *fixes)
{
    if (m_fixes == fixes)
        return;

    m_fixes = fixes;
    emit fixesChanged(fixes);
}

void System::setCellMatrix(QMatrix3x3 cellMatrix)
{
    if (m_cellMatrix == cellMatrix)
        return;

    m_cellMatrix = cellMatrix;
    emit cellMatrixChanged(cellMatrix);
}

void System::setBoundaryStyle(QString boundaryStyle)
{
    if (m_boundaryStyle == boundaryStyle)
        return;

    m_boundaryStyle = boundaryStyle;
    emit boundaryStyleChanged(boundaryStyle);
}

void System::setTriclinic(bool triclinic)
{
    if (m_triclinic == triclinic)
        return;

    m_triclinic = triclinic;
    emit triclinicChanged(triclinic);
}

void System::setPerformance(Performance *performance)
{
    if (m_performance == performance)
        return;

    m_performance = performance;
    emit performanceChanged(performance);
}

void System::setCpuremain(double cpuremain)
{
    if (m_cpuremain == cpuremain)
        return;

    m_cpuremain = cpuremain;
    emit cpuremainChanged(cpuremain);
}

void System::setDt(double dt)
{
    if (m_dt == dt)
        return;

    m_dt = dt;
    emit dtChanged(dt);
}

void System::setVariables(Variables *variables)
{
    if (m_variables == variables)
        return;

    m_variables = variables;
    emit variablesChanged(variables);
}
