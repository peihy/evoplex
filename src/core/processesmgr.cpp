/**
 * Copyright (C) 2016 - Marcos Cardinot
 * @author Marcos Cardinot <mcardinot@gmail.com>
 */

#include <QFutureWatcher>
#include <QList>
#include <QtConcurrent/QtConcurrentRun>
#include <QtDebug>

#include "core/processesmgr.h"

ProcessesMgr::ProcessesMgr()
    : m_threads(QThread::idealThreadCount())
{
}

ProcessesMgr::~ProcessesMgr()
{
    killAll();
}

int ProcessesMgr::add(Simulation* sim)
{
    int processId = m_processes.key(sim, -1);
    if (processId == -1) {
        processId = m_processes.isEmpty() ? 0 : m_processes.lastKey() + 1;
        m_processes.insert(processId, sim);
        sim->setProcessId(processId);
    }
    emit (newProcess(processId));
    return processId;
}

QList<int> ProcessesMgr::add(QList<Simulation*> sims)
{
    QList<int> processIds;
    int id = m_processes.lastKey();
    foreach (Simulation* sim, sims) {
        processIds.append(id);
        m_processes.insert(id, sim);
        sim->setProcessId(id);
        emit (newProcess(id));
        ++id;
    }
    return processIds;
}

int ProcessesMgr::addAndPlay(Simulation* sim)
{
    int id = add(sim);
    play(id);
    return id;
}

QList<int> ProcessesMgr::addAndPlay(QList<Simulation*> sims)
{
   QList<int> processIds = add(sims);
   play(processIds);
   return processIds;
}

void ProcessesMgr::play(int processId)
{
    if (m_runningProcesses.contains(processId)
            && m_queuedProcesses.contains(processId)) {
        return;
    } else if (!m_processes.contains(processId)) {
        qWarning() << "[Processes] tried to play an nonexistent process:" << processId;
        return;
    }

    if (m_runningProcesses.size() < m_threads) {
        m_runningProcesses.append(processId);

        QFutureWatcher<int>* watcher = new QFutureWatcher<int>(this);
        connect(watcher, SIGNAL(finished()), this, SLOT(threadFinished()));
        watcher->setFuture(QtConcurrent::run(this, &ProcessesMgr::runThread, processId));

        m_queuedProcesses.removeAt(processId);
    } else {
        m_queuedProcesses.append(processId);
    }
}

void ProcessesMgr::play(QList<int> processIds)
{
    foreach (int id, processIds) {
        play(id);
    }
}

void ProcessesMgr::pause(int processId)
{
    if (!m_runningProcesses.contains(processId) || !m_processes.contains(processId)) {
        return;
    }
    m_processes.value(processId)->pause();
}

void ProcessesMgr::pauseAt(int processId, quint64 step)
{
    if (!m_runningProcesses.contains(processId) || !m_processes.contains(processId)) {
        return;
    }
    m_processes.value(processId)->pauseAt(step);
}

void ProcessesMgr::stop(int processId)
{
    if (!m_runningProcesses.contains(processId) || !m_processes.contains(processId)) {
        return;
    }
    m_processes.value(processId)->stop();
}

void ProcessesMgr::stopAt(int processId, quint64 step)
{
    if (!m_runningProcesses.contains(processId) || !m_processes.contains(processId)) {
        return;
    }
    m_processes.value(processId)->stopAt(step);
}

int ProcessesMgr::runThread(int id)
{
    Simulation* sim = m_processes.value(id);
    sim->processSteps();
    return id;
}

// watcher
void ProcessesMgr::threadFinished()
{
    QFutureWatcher<int>* w = reinterpret_cast<QFutureWatcher<int>*>(sender());
    int id = w->result();
    m_runningProcesses.removeAt(id);
    delete w;

    // marked to kill?
    if (m_processesToKill.contains(id)) {
        kill(id);
    }

    // call next process in the queue
    if (!m_queuedProcesses.isEmpty()) {
        play(m_queuedProcesses.first());
    }
}

void ProcessesMgr::setNumThreads(int threads)
{
    if (m_threads == threads) {
        return;
    }

    const int p = qAbs(threads - m_threads);
    int old = m_threads;
    m_threads = threads;

    if (threads > old) {
        for (int i = 0; i < p && !m_queuedProcesses.isEmpty(); ++i) {
            int id = m_queuedProcesses.takeFirst();
            play(id);
        }
    } else if (threads < old) {
        for (int i = 0; i < p && !m_runningProcesses.isEmpty(); ++i) {
            int id = m_runningProcesses.takeFirst();
            pause(id);
            m_queuedProcesses.push_front(id);
        }
    }
}

void ProcessesMgr::kill(int id)
{
    if (m_runningProcesses.contains(id)) {
        m_processesToKill.append(id);
    } else {
        m_processesToKill.removeAt(id);
        delete m_processes.take(id);
        emit (killed(id));
    }
    m_queuedProcesses.removeAt(id); // just in case...
}

void ProcessesMgr::killAll()
{
    QList<int> ids = m_processes.keys();
    foreach (int id, ids) {
        kill(id);
    }
}