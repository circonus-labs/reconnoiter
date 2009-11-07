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

import com.espertech.esper.collection.SingleEventIterator;
import com.espertech.esper.core.StatementContext;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.EventType;
import com.espertech.esper.view.CloneableView;
import com.espertech.esper.view.DataWindowView;
import com.espertech.esper.view.View;
import com.espertech.esper.view.ViewSupport;

import java.util.Iterator;
import java.util.Arrays;

/**
 * This view is a moving window extending the specified number of elements into the past.
 */
public class DeriveView extends ViewSupport implements DataWindowView, CloneableView
{
    private StatementContext statementContext;
    protected ExprNode expressionX;
    protected ExprNode expressionY;
    private boolean isDouble;
    private WeightedValueBean lastWVBean;
    private NoitDerivePoint lastpoint;

    /**
     * Constructor creates a moving window extending the specified number of elements into the past.
     * @param size is the specified number of elements into the past
     * @param viewUpdatedCollection is a collection that the view must update when receiving events
     * @param deriveViewFactory for copying this view in a group-by
     */
    public DeriveView(StatementContext statementContext, ExprNode expressionX, ExprNode expressionY)
    {
        this.statementContext = statementContext;
        this.expressionX = expressionX;
        this.expressionY = expressionY;
        isDouble = (expressionY.getType() != double.class || expressionY.getType() != Double.class);
    }

    public View cloneView(StatementContext statementContext)
    {
        return new DeriveView(statementContext, this.expressionX, this.expressionY);
    }

    public boolean isEmpty()
    {
        return (lastWVBean == null);
    }

    public static EventType getEventType(com.espertech.esper.event.EventAdapterService eas)
    {
        return eas.addBeanType(WeightedValueBean.class.getName(), WeightedValueBean.class, false);
    }

    public EventType getEventType()
    {
        return getEventType(statementContext.getEventAdapterService());
    }

    public final void update(EventBean[] newData, EventBean[] oldData)
    {
        if(oldData != null)
            throw new UnsupportedOperationException("Time window does not accept event removal");

        int derivedsize = 0;
        EventBean[] derivedNew = null;
        if (newData != null) {
            int i = 0;
            derivedsize = newData.length;
            if (lastpoint == null) derivedsize -= 1;
            if(derivedsize > 0)
                derivedNew = new EventBean[derivedsize];
            for ( EventBean pointb : newData ) {
                EventBean eventsPerStream[] = { pointb };
                Number NX = (Number) expressionX.evaluate(eventsPerStream, true, statementContext);
                if(NX == null) continue;
                Number NY = (Number) expressionY.evaluate(eventsPerStream, true, statementContext);
                if(NY == null) continue;
                NoitDerivePoint point = new NoitDerivePoint();
                point.X = NX.longValue();
                if(isDouble)
                  point.Ydouble = NY.doubleValue();
                else
                  point.Ylong = NY.longValue();

                if (lastpoint != null) {
                    try {
                        NoitDerivePoint sub = subtract(point,lastpoint);
                        lastWVBean = new WeightedValueBean(sub.X, sub.ror(isDouble));
                        EventBean eb = statementContext.getEventAdapterService().adapterForBean(lastWVBean);
                        derivedNew[i++] = eb;
                    }
                    catch(Exception e) {
                        derivedsize--;
                    }
                }
                lastpoint = point;
            }
        }

        // If there are child views, call update method
        if (this.hasViews() && derivedNew != null)
        {
            updateChildren(derivedNew, null);
        }
    }

    protected NoitDerivePoint subtract(NoitDerivePoint a, NoitDerivePoint b)
    {
        NoitDerivePoint v = new NoitDerivePoint();
        v.X = a.X - b.X;
        v.Ylong = a.Ylong - b.Ylong;
        v.Ydouble = a.Ydouble - b.Ydouble;
        return v;
    }

    public final Iterator<EventBean> iterator()
    {
        return new SingleEventIterator(statementContext.getEventAdapterService().adapterForBean(lastWVBean));
    }

    public final String toString()
    {
        return this.getClass().getName() + " lastpoint=" + lastpoint.X + "," + (isDouble ? lastpoint.Ydouble : lastpoint.Ylong);
    }

    protected class NoitDerivePoint {
        public long X;
        public long Ylong;
        public double Ydouble;
        public double ror(boolean isDouble) {
            if(X == 0) return Double.NaN;
            if(isDouble) return (Ydouble / (double)X);
            else return ((double)Ylong / (double)X);
        }
    }
}
