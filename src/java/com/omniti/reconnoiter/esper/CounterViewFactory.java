/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.esper;

import java.util.List;
import com.espertech.esper.client.EventType;
import com.espertech.esper.epl.expression.ExprNode;
import com.espertech.esper.view.ViewParameterException;
import com.espertech.esper.view.ViewFactorySupport;
import com.espertech.esper.view.ViewFactoryContext;
import com.espertech.esper.view.ViewFactory;
import com.espertech.esper.view.View;
import com.espertech.esper.core.StatementContext;

public class CounterViewFactory extends ViewFactorySupport {
    private ViewFactoryContext viewFactoryContext;
    private List<ExprNode> viewParameters;
    private ExprNode timestampExpression;
    private ExprNode valueExpression;

    public void setViewParameters(ViewFactoryContext viewFactoryContext, 
                                  List<ExprNode> viewParameters) throws ViewParameterException {
        this.viewFactoryContext = viewFactoryContext;
        if (viewParameters.size() != 2) {
            throw new ViewParameterException(
                "View requires a two parameters: x, y");
        }
        this.viewParameters = viewParameters;
    }

    public void attach(EventType parentEventType, 
		StatementContext statementContext, 
		ViewFactory optionalParentFactory, 
		List<ViewFactory> parentViewFactories) throws ViewParameterException {
    		
        ExprNode[] validatedNodes = ViewFactorySupport.validate("CounterView", 
    	      parentEventType, statementContext, viewParameters, false);
    
        timestampExpression = validatedNodes[0];
        valueExpression = validatedNodes[1];
    
        if ((timestampExpression.getExprEvaluator().getType() != long.class) && 
            (timestampExpression.getExprEvaluator().getType() != Long.class)) {
            throw new ViewParameterException(
                "View requires long-typed timestamp values in parameter 1");
        }
        if ((valueExpression.getExprEvaluator().getType() != double.class) && 
            (valueExpression.getExprEvaluator().getType() != Double.class) &&
            (valueExpression.getExprEvaluator().getType() != long.class) && 
            (valueExpression.getExprEvaluator().getType() != Long.class)) {
            throw new ViewParameterException(
                "View requires long-typed or double-typed values for in parameter 2");
        }
    }
    public View makeView(StatementContext statementContext) {
        return new CounterView(statementContext, timestampExpression, valueExpression);
    }

    public EventType getEventType() {
        return CounterView.getEventType(viewFactoryContext.getEventAdapterService());
    }
}
