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

import com.omniti.reconnoiter.esper.DeriveView;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.view.View;
import com.espertech.esper.client.EventType;
import com.espertech.esper.core.context.util.AgentInstanceContext;
import com.espertech.esper.view.stat.StatViewAdditionalProps;

/**
 * This view is a moving window extending the specified number of elements into the past.
 */
public class CounterView extends DeriveView
{
    public CounterView(AgentInstanceContext agentInstanceContext, ExprNode expressionX, ExprNode expressionY, EventType eventType, StatViewAdditionalProps additionalProps)
    {
        super(agentInstanceContext, expressionX, expressionY, eventType, additionalProps);
    }
    public View cloneView()
    {
        return new CounterView(this.agentInstanceContext, this.expressionX, this.expressionY, this.eventType, this.additionalProps);
    }
    protected NoitDerivePoint subtract(NoitDerivePoint a, NoitDerivePoint b)
    {
        NoitDerivePoint v = super.subtract(a,b);
        if(v.Ylong < 0 || v.Ydouble < 0.0) v.X = 0;
        return v;
    }
}
