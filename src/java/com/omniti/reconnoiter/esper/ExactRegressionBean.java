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

import java.math.BigDecimal;
import java.math.MathContext;

public final class ExactRegressionBean extends ExactBaseStatisticsBean
{
    private static final long serialVersionUID = -3558474554218814231L;

    public double getYIntercept()
    {
        double sloped = getSlope();
        if (Double.isNaN(sloped)) return sloped;
        BigDecimal slope = new BigDecimal(sloped);
        return getYSum().divide(new BigDecimal(getN()), MathContext.DECIMAL128)
                        .subtract(slope.multiply(getXSum()).divide(new BigDecimal(getN()), MathContext.DECIMAL128))
                        .doubleValue();
    }

    public double getSlope()
    {
        try {
            if (this.getN() == 0) return Double.NaN;
            BigDecimal ssxi = getXSum().multiply(getXSum()).divide(new BigDecimal(getN()), MathContext.DECIMAL128);
            BigDecimal ssx = getSumXSq().subtract(ssxi);
            if (ssx.compareTo(BigDecimal.ZERO) == 0) return Double.NaN;
            BigDecimal spi = getXSum().multiply(getYSum()).divide(new BigDecimal(getN(), MathContext.DECIMAL128));
            BigDecimal sp = getSumXY().subtract(spi);
            return sp.divide(ssx, MathContext.DECIMAL128).doubleValue();
        }
        catch(ArithmeticException ae) {
            ae.printStackTrace();
            return Double.NaN;
        }
    }

}
