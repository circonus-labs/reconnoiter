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
import com.espertech.esper.core.StatementContext;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.view.View;

/**
 * This view is a moving window extending the specified number of elements into the past.
 */
public class CounterView extends DeriveView
{
    public CounterView(StatementContext statementContext, ExprNode expressionX, ExprNode expressionY)
    {
        super(statementContext, expressionX, expressionY);
    }
    public View cloneView(StatementContext statementContext)
    {
        return new CounterView(statementContext, this.expressionX, this.expressionY);
    }
    protected NoitDerivePoint subtract(NoitDerivePoint a, NoitDerivePoint b)
    {
        NoitDerivePoint v = super.subtract(a,b);
        if(v.Ylong < 0 || v.Ydouble < 0.0) v.X = 0;
        return v;
    }
}
