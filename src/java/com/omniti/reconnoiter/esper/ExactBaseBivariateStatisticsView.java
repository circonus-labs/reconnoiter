/**************************************************************************************
 * Copyright (C) 2008 EsperTech, Inc. All rights reserved.                            *
 * Copyright (C) 2008 OmniTI, Inc. All rights reserved.                               *
 * http://esper.codehaus.org                                                          *
 * http://www.espertech.com                                                           *
 * ---------------------------------------------------------------------------------- *
 * The software in this package is published under the terms of the GPL license       *
 * a copy of which has been included with this distribution in the license.txt file.  *
 **************************************************************************************/

package com.omniti.reconnoiter.esper;

import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.EventType;
import com.espertech.esper.collection.SingleEventIterator;
import com.espertech.esper.core.context.util.AgentInstanceContext;
import com.espertech.esper.epl.expression.ExprEvaluator;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.event.EventAdapterService;
import com.espertech.esper.view.ViewSupport;
import com.espertech.esper.view.stat.StatViewAdditionalProps;

import java.util.Iterator;
import java.math.BigDecimal;

public abstract class ExactBaseBivariateStatisticsView extends ViewSupport
{
    /**
     * This bean can be overridden by subclasses providing extra values such as correlation, regression.
     */
    protected ExactRegressionBean statisticsBean;

    private ExprNode expressionX;
    private ExprEvaluator expressionXEval;
    private ExprNode expressionY;
    private ExprEvaluator expressionYEval;
    private boolean  isDouble;
    private EventBean[] eventsPerStream = new EventBean[1];

    protected AgentInstanceContext agentInstanceContext;
    protected final StatViewAdditionalProps additionalProps;

    private EventBean lastNewEvent;
    protected final EventType eventType;

    public ExactBaseBivariateStatisticsView(AgentInstanceContext agentInstanceContext,
                                       ExprNode expressionX,
                                       ExprNode expressionY,
                                       EventType eventType,
                                       StatViewAdditionalProps additionalProps
                                       )
    {
        this.agentInstanceContext = agentInstanceContext;
        this.expressionX = expressionX;
        this.expressionXEval = expressionX.getExprEvaluator();
        this.expressionY = expressionY;
        this.expressionYEval = expressionY.getExprEvaluator();
        this.eventType = eventType;
        this.additionalProps = additionalProps;
    }

    public void update(EventBean[] newData, EventBean[] oldData)
    {
        // If we have child views, keep a reference to the old values, so we can fireStatementStopped them as old data event.
        ExactRegressionBean oldValues = null;
        if (lastNewEvent == null)
        {
            if (this.hasViews())
            {
                oldValues = (ExactRegressionBean) statisticsBean.clone();
            }
        }

        // add data points to the bean
        if (newData != null)
        {
            for (int i = 0; i < newData.length; i++)
            {
                eventsPerStream[0] = newData[i];
                BigDecimal X = new BigDecimal(((Number) expressionX.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).toString());
                BigDecimal Y;
                if(isDouble)
                  Y = new BigDecimal(((Number) expressionY.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).doubleValue());
                else
                  Y = new BigDecimal(((Number) expressionY.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).toString());
                statisticsBean.addPoint(X, Y);
            }
        }

        // remove data points from the bean
        if (oldData != null)
        {
            for (int i = 0; i < oldData.length; i++)
            {
                eventsPerStream[0] = oldData[i];
                BigDecimal X = new BigDecimal(((Number) expressionX.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).toString());
                BigDecimal Y;
                if(isDouble)
                  Y = new BigDecimal(((Number) expressionY.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).doubleValue());
                else
                  Y = new BigDecimal(((Number) expressionY.getExprEvaluator().evaluate(eventsPerStream, true, agentInstanceContext)).toString());
                statisticsBean.removePoint(X, Y);
            }
        }
        if (this.hasViews())
        {
            if (lastNewEvent == null)
            {
                ExactRegressionBean newValues = (ExactRegressionBean) statisticsBean.clone();
                EventBean newValuesEvent = agentInstanceContext.getStatementContext().getEventAdapterService().adapterForBean(newValues);
                EventBean oldValuesEvent = agentInstanceContext.getStatementContext().getEventAdapterService().adapterForBean(oldValues);
                updateChildren(new EventBean[] {newValuesEvent}, new EventBean[] {oldValuesEvent});
                lastNewEvent = newValuesEvent;
            }
            else
            {
                ExactRegressionBean newValues = (ExactRegressionBean) statisticsBean.clone();
                EventBean newValuesEvent = agentInstanceContext.getStatementContext().getEventAdapterService().adapterForBean(newValues);
                updateChildren(new EventBean[] {newValuesEvent}, new EventBean[] {lastNewEvent});
                lastNewEvent = newValuesEvent;
            }
        }
    }

    public final Iterator<EventBean> iterator()
    {
        return new SingleEventIterator(agentInstanceContext.getStatementContext().getEventAdapterService().adapterForBean(statisticsBean));
    }

    public final ExprNode getExpressionX()
    {
        return expressionX;
    }

    public final ExprNode getExpressionY()
    {
        return expressionY;
    }
}
