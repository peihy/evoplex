/**
 * Copyright (C) 2016 - Marcos Cardinot
 * @author Marcos Cardinot <mcardinot@gmail.com>
 */

#ifndef ABSTRACT_GRAPH_H
#define ABSTRACT_GRAPH_H

#include <QObject>
#include <QVariantHash>
#include <QVector>
#include <QtPlugin>

#include "core/abstractagent.h"

// one agent can be linked to N neighbours which are also agents in the graph
typedef const AbstractAgent* Neighbour; // alias

class Edge
{
public:
    Edge(Neighbour neighbour): m_neighbour(neighbour) {}
    Edge(Neighbour neighbour, QVariantHash& attributes): m_neighbour(neighbour), m_attributes(attributes) {}
    inline Neighbour getNeighbour() { return m_neighbour; }
    inline const QVariant getAttribute(const QString& name) { return m_attributes.value(name); }
private:
    Neighbour m_neighbour;
    QVariantHash m_attributes;
};

typedef QVector<Edge*> Neighbours;  // neighbourhood of one agent
typedef QHash<int, Neighbours> AdjacencyList;
typedef QHash<int, AbstractAgent*> Population;

class AbstractGraph
{
public:
    // constructor
    explicit AbstractGraph(const QString& name): m_graphName(name) {}

    // Provide the destructor to keep compilers happy.
    virtual ~AbstractGraph() {}

    // Initializes the graph object.
    // This method is called once when a new graph object is being created.
    // It is usually used to validate the graphParams and the set of agents.
    virtual bool init(QVector<AbstractAgent*> agents, const QVariantHash& graphParams) = 0;

    // Reset the neighbourhood of all agents to the original structure.
    virtual void resetNetwork() = 0;

    // This method is used to introduce spatial coordinates for each agent.
    // It is mainly used by the GUI when it wants to draw the graph.
    // If returns false, GUI will not draw it.
    virtual bool buildCoordinates() = 0;

    // return the current value of all graph parameters (if any)
    // eg., height, width ...
    virtual QVariantHash getGraphParams() const = 0;

    // getters
    inline const QString& getGraphName() { return m_graphName; }
    inline AbstractAgent* getAgent(int id) { return m_population.value(id, NULL); }
    inline Neighbours getNeighbours(int id) { return m_adjacencyList.value(id); }
    inline Population getPopulation() { return m_population; }

protected:
    const QString m_graphName;
    AdjacencyList m_adjacencyList;
    Population m_population;
};


class IPluginGraph
{
public:
    // provide the destructor to keep compilers happy.
    virtual ~IPluginGraph() {}

    // create the real graph object.
    virtual AbstractGraph* create() = 0;
};
Q_DECLARE_INTERFACE(IPluginGraph, "org.evoplex.IPluginGraph")


#define REGISTER_GRAPH(CLASSNAME)                                           \
    class PG_##CLASSNAME: public QObject, public IPluginGraph               \
    {                                                                       \
    Q_OBJECT                                                                \
    Q_PLUGIN_METADATA(IID "org.evoplex.IPluginGraph"                        \
                      FILE "graphMetaData.json")                            \
    Q_INTERFACES(IPluginGraph)                                              \
    public:                                                                 \
        AbstractGraph* create() {                                           \
            return dynamic_cast<AbstractGraph*>(new CLASSNAME(#CLASSNAME)); \
        }                                                                   \
    };

#endif // ABSTRACT_GRAPH_H
