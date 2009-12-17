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

import com.espertech.esper.collection.SingleEventIterator;
import com.espertech.esper.core.StatementContext;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.view.ViewSupport;

import java.util.Iterator;
import java.math.BigDecimal;

public abstract class ExactBaseBivariateStatisticsView extends ViewSupport
{
    /**
     * This bean can be overridden by subclasses providing extra values such as correlation, regression.
     */
    protected ExactRegressionBean statisticsBean;

    private ExprNode expressionX;
    private ExprNode expressionY;
    private boolean  isDouble;
    private EventBean[] eventsPerStream = new EventBean[1];

    protected StatementContext statementContext;

    private EventBean lastNewEvent;

    public ExactBaseBivariateStatisticsView(StatementContext statementContext,
                                            ExactRegressionBean statisticsBean,
                                            ExprNode expressionX,
                                            ExprNode expressionY)
    {
        this.statementContext = statementContext;
        this.statisticsBean = statisticsBean;
        this.expressionX = expressionX;
        this.expressionY = expressionY;
        isDouble = (expressionY.getType() != double.class || expressionY.getType() != Double.class);
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
                BigDecimal X = new BigDecimal(((Number) expressionX.evaluate(eventsPerStream, true, statementContext)).toString());
                BigDecimal Y;
                if(isDouble)
                  Y = new BigDecimal(((Number) expressionY.evaluate(eventsPerStream, true, statementContext)).doubleValue());
                else
                  Y = new BigDecimal(((Number) expressionY.evaluate(eventsPerStream, true, statementContext)).toString());
                statisticsBean.addPoint(X, Y);
            }
        }

        // remove data points from the bean
        if (oldData != null)
        {
            for (int i = 0; i < oldData.length; i++)
            {
                eventsPerStream[0] = oldData[i];
                BigDecimal X = new BigDecimal(((Number) expressionX.evaluate(eventsPerStream, true, statementContext)).toString());
                BigDecimal Y;
                if(isDouble)
                  Y = new BigDecimal(((Number) expressionY.evaluate(eventsPerStream, true, statementContext)).doubleValue());
                else
                  Y = new BigDecimal(((Number) expressionY.evaluate(eventsPerStream, true, statementContext)).toString());
                statisticsBean.removePoint(X, Y);
            }
        }
        if (this.hasViews())
        {
            if (lastNewEvent == null)
            {
                ExactRegressionBean newValues = (ExactRegressionBean) statisticsBean.clone();
                EventBean newValuesEvent = statementContext.getEventAdapterService().adapterForBean(newValues);
                EventBean oldValuesEvent = statementContext.getEventAdapterService().adapterForBean(oldValues);
                updateChildren(new EventBean[] {newValuesEvent}, new EventBean[] {oldValuesEvent});
                lastNewEvent = newValuesEvent;
            }
            else
            {
                ExactRegressionBean newValues = (ExactRegressionBean) statisticsBean.clone();
                EventBean newValuesEvent = statementContext.getEventAdapterService().adapterForBean(newValues);
                updateChildren(new EventBean[] {newValuesEvent}, new EventBean[] {lastNewEvent});
                lastNewEvent = newValuesEvent;
            }
        }
    }

    public final Iterator<EventBean> iterator()
    {
        return new SingleEventIterator(statementContext.getEventAdapterService().adapterForBean(statisticsBean));
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
