/* Evoplex <https://evoplex.org>
 * Copyright (C) 2016-present - Marcos Cardinot <marcos@cardinot.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QVector>
#include <QStringList>
#include <QTextStream>
#include <set>

#include "project.h"
#include "experiment.h"
#include "utils.h"

namespace evoplex {

Project::Project(MainApp* mainApp, int id)
    : m_mainApp(mainApp),
      m_id(id),
      m_hasUnsavedChanges(false)
{
}

Project::~Project()
{
    for (auto& e : m_experiments) {
        e.second->invalidate();
    }
    m_experiments.clear();
}

bool Project::init(QString& error, const QString& filepath)
{
    Q_ASSERT_X(m_experiments.empty(), "Project", "a project cannot be initialized twice");
    setFilePath(filepath);
    if (!filepath.isEmpty()) {
        blockSignals(true);
        importExperiments(filepath, error);
        blockSignals(false);
    }
    m_hasUnsavedChanges = false;
    return !error.isEmpty();
}

void Project::setFilePath(const QString& path)
{
    m_filepath = path;
    QString name = m_name;
    m_name = path.isEmpty() ? QString("Project%1").arg(m_id)
                            : QFileInfo(path).baseName();
    if (name != m_name) {
        emit (nameChanged(m_name));
    }
}

void Project::playAll()
{
    for (auto& i : m_experiments)
        i.second->play();
}

void Project::pauseAll()
{
    for (auto& i : m_experiments) {
        if (i.second->expStatus() == Status::Running ||
                i.second->expStatus() == Status::Queued) {
            i.second->pause();
        }
    }
}

int Project::generateExpId() const
{
    return m_experiments.empty() ? 0 : (--m_experiments.end())->first + 1;
}

ExperimentPtr Project::newExperiment(ExpInputsPtr inputs, QString& error)
{
    QMutexLocker locker(&m_mutex);

    if (!inputs) {
        error += "Null inputs!";
        return nullptr;
    }

    int expId = inputs->general(GENERAL_ATTR_EXPID).toInt();
    if (m_experiments.count(expId)) {
        error += "The Experiment Id must be unique!";
        return nullptr;
    }

    ExperimentPtr exp = std::make_shared<Experiment>(m_mainApp, expId, shared_from_this());
    m_experiments.insert({expId, exp});
    exp->setInputs(std::move(inputs), error);

    m_hasUnsavedChanges = true;
    emit (hasUnsavedChanges(m_hasUnsavedChanges));
    emit (expAdded(expId));
    return exp;
}

bool Project::removeExperiment(int expId, QString& error)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_experiments.find(expId);
    if (it == m_experiments.cend()) {
        error += "tried to remove a nonexistent experiment";
        return false;
    }

    ExperimentPtr exp = (*it).second;
    if (m_experiments.erase(exp->id()) < 1) {
        error += "failed to remove the experiment";
        return false;
    }

    emit (expRemoved(expId));
    exp->invalidate();

    m_hasUnsavedChanges = true;
    emit (hasUnsavedChanges(m_hasUnsavedChanges));
    return true;
}

bool Project::editExperiment(int expId, ExpInputsPtr newInputs, QString& error)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_experiments.find(expId);
    if (it == m_experiments.cend()) {
        error += "tried to edit a nonexistent experiment";
        return false;
    }
    if (!(*it).second->setInputs(std::move(newInputs), error)) {
        return false;
    }
    m_hasUnsavedChanges = true;
    emit (hasUnsavedChanges(m_hasUnsavedChanges));
    emit (expEdited(expId));
    return true;
}

int Project::importExperiments(const QString& filePath, QString& error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error += QString("Couldn't read the experiments from:\n'%1'\n"
                 "Please, make sure it is a readable csv file.\n").arg(filePath);
        qWarning() << error;
        return 0;
    }

    QTextStream in(&file);

    // read header
    const QStringList header = in.readLine().split(",");
    if (header.isEmpty()) {
        error += QString("Couldn't read the experiments from:\n'%1'\n"
                 "The header must have the following columns: %2\n")
                 .arg(filePath, m_mainApp->generalAttrsScope().keys().join(", "));
        qWarning() << error;
        return 0;
    }

    // import experiments
    int row = 1;
    while (!in.atEnd()) {
        const QStringList values = in.readLine().split(",");
        QString expErrorMsg;
        auto inputs = ExpInputs::parse(m_mainApp, header, values, expErrorMsg);
        if (!expErrorMsg.isEmpty()) {
            error += QString("Row %1 : Warning: %2\n\n").arg(row).arg(expErrorMsg);
        }
        expErrorMsg.clear();
        if (!inputs || !newExperiment(std::move(inputs), expErrorMsg)) {
            error += QString("Row %1 (skipped): Critical error: %2\n\n").arg(row).arg(expErrorMsg);
        }
        if (!expErrorMsg.isEmpty()) {
            error += QString("Row %1 : Warning: %2\n\n").arg(row).arg(expErrorMsg);
        }
        ++row;
    }
    file.close();

    if (row == 1) {
        error += QString("This file is empty.\n"
                 "There were no experiments to be read.\n'%1'\n").arg(filePath);
    }

    if (!error.isEmpty()) {
        error += QString("`%1`\n").arg(filePath);
        qWarning() << error;
    }

    return row - 1;
}

bool Project::saveProject(QString& errMsg, std::function<void(int)>& progress)
{
    if (m_experiments.empty()) {
        errMsg = QString("Unable to save the project '%1'.\n"
                "This project is empty. There is nothing to save.").arg(name());
        qWarning() << errMsg;
        return false;
    }

    QFile file(m_filepath);
    QFileInfo fi(file);
    if (fi.suffix() != "csv" || !file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        errMsg = QString("Unable to save the project '%1'.\n"
                "Please, make sure the path below corresponds to a writable csv file!\n%2")
                .arg(name(), m_filepath);
        qWarning() << errMsg;
        return false;
    }

    const float kProgress = (2.f * m_experiments.size()) / 100.f;
    int _progress = 0;

    // join the header of all experiments
    std::vector<QString> header;
    QString lModelId, lGraphId;
    for (auto const& i : m_experiments) {
        if (i.second->modelId() == lModelId && i.second->graphId() == lGraphId) {
            continue;
        }
        lModelId = i.second->modelId();
        lGraphId = i.second->graphId();
        std::vector<QString> h = i.second->inputs()->exportAttrNames(false);
        header.insert(header.end(), h.begin(), h.end());
        _progress += kProgress;
        progress(_progress);
    }
    // remove duplicates
    std::set<QString> s(header.begin(), header.end());
    header.assign(s.begin(), s.end());

    // for convenience, we move the 'id' to the first column
    for (auto it = header.begin(); it != header.end(); ++it) {
        if (*it == GENERAL_ATTR_EXPID) {
            header.erase(it);
            break;
        }
    }
    header.insert(header.begin(), GENERAL_ATTR_EXPID);

    // write header to file
    QTextStream out(&file);
    for (size_t h = 0; h < header.size()-1; ++h) {
        out << header.at(h) << ",";
    }
    out << header.at(header.size() - 1) << "\n";

    // write values to file
    for (auto const& i : m_experiments) {
        const ExperimentPtr exp = i.second;
        const ExpInputs* inputs = exp->inputs();
        const QString modelId_ = exp->modelId() + "_";
        const QString graphId_ = exp->graphId() + "_";

        QString values;
        for (QString attrName : header) {
            Value v;
            if (attrName.startsWith(modelId_)) {
                v = inputs->model(attrName.remove(modelId_));
            } else if (attrName.startsWith(graphId_)) {
                v = inputs->graph(attrName.remove(graphId_));
            } else {
                v = inputs->general(attrName);
            }
            values.append(v.toQString() + ","); // will leave empty if not found
        }
        values.remove(values.size()-1, 1); // remove last comma
        out << values << "\n";

        _progress += kProgress;
        progress(_progress);
    }
    file.close();

    m_mainApp->addPathToRecentProjects(m_filepath);

    m_hasUnsavedChanges = false;
    emit (hasUnsavedChanges(false));
    progress((100));
    qDebug() << "a project has been saved!" << name();
    return true;
}

}
