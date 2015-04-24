#include "simulation.h"
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
using std::function;

Simulation::Simulation()
{
    setScaleAndColorEvaluator();
}

QString Simulation::readFile(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Could not open file: " << file.fileName();
        return "";
    }

    return file.readAll();
}

void Simulation::runCommand(LAMMPS *lammps, const char *command)
{
    if(lammps == 0) {
        qDebug() << "Warning, trying to run a LAMMPS command with no LAMMPS object.";
        qDebug() << "Command: " << command;
        return;
    }

    lammps_command((void*)lammps, (char*) command);
}

QString Simulation::copyDataFileToReadablePath(QString filename)
{
    QFile inFile(":/scripts/"+filename);
    if(!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Error: could not open file "+inFile.fileName();
        return "";
    }

    QString content = inFile.readAll();
    inFile.close();

    QString documentsLocation = QStandardPaths::locate(QStandardPaths::TempLocation, QString(), QStandardPaths::LocateDirectory);
    QString newFilename=documentsLocation+filename;
    QFile outFile(newFilename);
    if(!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Error: could not open file "+outFile.fileName();
        return "";
    }

    outFile.write(content.toStdString().c_str());
    outFile.close();
    return newFilename;
}

void Simulation::setScaleAndColorEvaluator()
{
    m_scaleAndColorEvaluator = [](QVector<QColor> &colors, QVector<float> &scales, LAMMPS *lammps) {
        QColor color1(255.0, 0.0, 0.0);
        QColor color2(0.0, 255.0, 0.0);
        QColor color3(0.0, 0.0, 255.0);
        QColor color4(0.0, 255.0, 255.0);
        QColor color5(255.0, 255.0, 0.0);


        for(unsigned int i=0; i<lammps->atom->natoms; i++) {
            if(lammps->atom->type[i] == 1) colors[i] = color1;
            else if(lammps->atom->type[i] == 2) colors[i] = color2;
            else if(lammps->atom->type[i] == 3) colors[i] = color3;
            else if(lammps->atom->type[i] == 4) colors[i] = color4;
            else if(lammps->atom->type[i] == 5) colors[i] = color5;
        }
    };
}

void Simulation::initialize(QString inputScriptFile, QVector3D initialCameraPosition, QVector3D deltaPosition)
{
    m_inputScriptFile = inputScriptFile;
    m_positionOffset = deltaPosition;
    m_initialCameraPosition = initialCameraPosition;
    m_isInitialized = true;
}

function<void (QVector<QColor> &colors, QVector<float> &scales, LAMMPS *lammps)> Simulation::scaleAndColorEvaluator()
{
    return m_scaleAndColorEvaluator;
}

QVector3D Simulation::positionOffset()
{
    return m_positionOffset;
}

void Simulation::processCommand(std::stringstream &command, LAMMPS *lammps) {
    std::string word;
    QString processedCommand;

    while(command >> word) {
        if(word.find("ext://") != std::string::npos) {
            word.erase(0, 6); // Remove the ext:// prefix
            word = copyDataFileToReadablePath(QString::fromStdString(word)).toStdString();
        }

        processedCommand.append(QString::fromStdString(word)).append(" ");
    }
    runCommand(lammps, processedCommand.toStdString().c_str());
}

void Simulation::runLammpsScript(LAMMPS *lammps)
{
    if(!m_isInitialized) {
        qDebug() << "Warning, trying to run lammps script for a simulation that is not initialized. Remember to call initialize() function in Simulation constructor.";
        return;
    }

    QString lammpsScript_qstring = readFile(m_inputScriptFile);

    if (!lammpsScript_qstring.isEmpty())
    {
        std::stringstream lammpsScript(lammpsScript_qstring.toStdString());
        std::string line;

        while(std::getline(lammpsScript,line,'\n')){
            std::stringstream command(line);
            processCommand(command, lammps);
        }
    }
}

QVector3D Simulation::initialCameraPosition() const
{
    return m_initialCameraPosition;
}
