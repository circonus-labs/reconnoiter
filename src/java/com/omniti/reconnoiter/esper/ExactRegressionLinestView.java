/**************************************************************************************
 * Copyright (C) 2008 EsperTech, Inc. All rights reserved.                            *
 * Copyright (C) 2009 OmniTI, Inc. All rights reserved.                               *
 * http://esper.codehaus.org                                                          *
 * http://www.espertech.com                                                           *
 * ---------------------------------------------------------------------------------- *
 * The software in this package is published under the terms of the GPL license       *
 * a copy of which has been included with this distribution in the license.txt file.  *
 **************************************************************************************/
package com.omniti.reconnoiter.esper;

import com.espertech.esper.client.EventType;
import com.espertech.esper.core.StatementContext;
import com.espertech.esper.view.CloneableView;
import com.espertech.esper.view.View;
import com.espertech.esper.epl.expression.ExprNode;

public final class ExactRegressionLinestView extends ExactBaseBivariateStatisticsView implements CloneableView
{
    private EventType eventType;

    public ExactRegressionLinestView(StatementContext statementContext, ExprNode xFieldName, ExprNode yFieldName)
    {
        super(statementContext, new ExactRegressionBean(), xFieldName, yFieldName);
    }

    public View cloneView(StatementContext statementContext)
    {
        return new ExactRegressionLinestView(statementContext, this.getExpressionX(), this.getExpressionY());
    }

    public static EventType getEventType(com.espertech.esper.event.EventAdapterService eas)
    {
        return eas.addBeanType(ExactRegressionBean.class.getName(), ExactRegressionBean.class, false);
    }

    public EventType getEventType()
    {
        return getEventType(statementContext.getEventAdapterService());
    }

    public String toString()
    {
        return this.getClass().getName() +
                " fieldX=" + this.getExpressionX() +
                " fieldY=" + this.getExpressionY();
    }
}

