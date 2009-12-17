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

import com.espertech.esper.view.ViewProcessingException;
import com.espertech.esper.client.EPException;
import com.espertech.esper.util.MetaDefItem;

import java.io.Serializable;
import java.math.BigDecimal;
import java.math.MathContext;

/**
 * Bean for performing statistical calculations. The bean keeps sums of X and Y datapoints and sums on squares
 * that can be reused by subclasses. The bean calculates standard deviation (sample and population), variance,
 * average and sum.
  */
public class ExactBaseStatisticsBean implements Cloneable, Serializable
{
    private BigDecimal sumX;
    private BigDecimal sumXSq;
    private BigDecimal sumY;
    private BigDecimal sumYSq;
    private BigDecimal sumXY;
    private long dataPoints;
    private static final long serialVersionUID = 7985123761256377184L;

    private void initialize()
    {
        sumX = 
        sumXSq = 
        sumY = 
        sumYSq =
        sumXY = BigDecimal.ZERO;
        dataPoints = 0;
    }

    public ExactBaseStatisticsBean()
    {
        super();
        initialize();
    }

    /**
     * Add a data point for the X data set only.
     * @param x is the X data point to add.
     */
    public final void addPoint(BigDecimal x)
    {
        dataPoints++;
        sumX = sumX.add(x);
        sumXSq = sumXSq.add(x.multiply(x));
    }

    /**
     * Add a data point.
     * @param x is the X data point to add.
     * @param y is the Y data point to add.
     */
    public final void addPoint(BigDecimal x, BigDecimal y)
    {
        dataPoints++;
        sumX = sumX.add(x);
        sumXSq = sumXSq.add(x.multiply(x));
        sumY = sumY.add(y);
        sumYSq = sumYSq.add(y.multiply(y));
        sumXY = sumXY.add(x.multiply(y));
    }

    /**
     * Remove a X data point only.
     * @param x is the X data point to remove.
     */
    public final void removePoint(BigDecimal x)
    {
        dataPoints--;
        if (dataPoints <= 0)
        {
            initialize();
        }
        else
        {
            sumX = sumX.subtract(x);
            sumXSq = sumXSq.subtract(x.multiply(x));
        }
    }

    /**
     * Remove a data point.
     * @param x is the X data point to remove.
     * @param y is the Y data point to remove.
     */
    public final void removePoint(BigDecimal x, BigDecimal y)
    {
        dataPoints--;
        if (dataPoints <= 0)
        {
            initialize();
        }
        else
        {
            sumX = sumX.subtract(x);
            sumXSq = sumXSq.subtract(x.multiply(x));
            sumY = sumY.subtract(y);
            sumYSq = sumYSq.subtract(y.multiply(y));
            sumXY = sumXY.subtract(x.multiply(y));
        }
    }

    public final BigDecimal getXStandardDeviationPop()
    {
        if (dataPoints == 0) return null;
        BigDecimal temp = sumXSq.subtract(sumX.multiply(sumX).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128)).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128);
        return new BigDecimal(Math.sqrt(temp.doubleValue()));
    }

    public final BigDecimal getYStandardDeviationPop()
    {
        if (dataPoints == 0) return null;
        BigDecimal temp = sumYSq.subtract(sumY.multiply(sumY).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128)).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128);
        return new BigDecimal(Math.sqrt(temp.doubleValue()));
    }

    public final BigDecimal getXStandardDeviationSample()
    {
        if (dataPoints < 2) return null;

        BigDecimal variance = getXVariance();
        return new BigDecimal(Math.sqrt(variance.doubleValue()));
    }

    /**
     * Calculates standard deviation for Y based on the sample data points supplied.
     * Equivalent to Microsoft Excel formula STDEV.
     * @return standard deviation assuming sample for Y
     */
    public final BigDecimal getYStandardDeviationSample()
    {
        if (dataPoints < 2) return BigDecimal.ZERO;

        BigDecimal variance = getYVariance();
        return new BigDecimal(Math.sqrt(variance.doubleValue()));
    }

    /**
     * Calculates standard deviation for X based on the sample data points supplied.
     * Equivalent to Microsoft Excel formula STDEV.
     * @return variance as the square of the sample standard deviation for X
     */
    public final BigDecimal getXVariance()
    {
        if (dataPoints < 2) return null;
        return sumXSq.subtract(sumX.multiply(sumX).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128)).divide(new BigDecimal(dataPoints - 1), MathContext.DECIMAL128);
    }

    /**
     * Calculates standard deviation for Y based on the sample data points supplied.
     * Equivalent to Microsoft Excel formula STDEV.
     * @return variance as the square of the sample standard deviation for Y
     */
    public final BigDecimal getYVariance()
    {
        if (dataPoints < 2) return null;
        return sumYSq.subtract(sumY.multiply(sumY).divide(new BigDecimal(dataPoints), MathContext.DECIMAL128)).divide(new BigDecimal(dataPoints - 1), MathContext.DECIMAL128);
    }

    /**
     * Returns the number of data points.
     * @return number of data points
     */
    public final long getN()
    {
        return dataPoints;
    }

    /**
     * Returns the sum of all X data points.
     * @return sum of X data points
     */
    public final BigDecimal getXSum()
    {
        return sumX;
    }

    /**
     * Returns the sum of all Y data points.
     * @return sum of Y data points
     */
    public final BigDecimal getYSum()
    {
        return sumY;
    }

    /**
     * Returns the average of all X data points.
     * @return average of X data points
     */
    public final BigDecimal getXAverage()
    {
        if (dataPoints == 0) return null;
        return sumX.divide(new BigDecimal(dataPoints), MathContext.DECIMAL128);
    }

    /**
     * Returns the average of all Y data points.
     * @return average of Y data points
     */
    public final BigDecimal getYAverage()
    {
        if (dataPoints == 0) return null;
        return sumY.divide(new BigDecimal(dataPoints), MathContext.DECIMAL128);
    }

    /**
     * For use by subclasses, returns sum (X * X).
     * @return sum of X squared
     */
    public final BigDecimal getSumXSq()
    {
        return sumXSq;
    }

    /**
     * For use by subclasses, returns sum (Y * Y).
     * @return sum of Y squared
     */
    public final BigDecimal getSumYSq()
    {
        return sumYSq;
    }

    /**
     * For use by subclasses, returns sum (X * Y).
     * @return sum of X times Y
     */
    public final BigDecimal getSumXY()
    {
        return sumXY;
    }

    public final Object clone()
    {
        try
        {
            return super.clone();
        }
        catch (CloneNotSupportedException e)
        {
            throw new EPException(e);
        }
    }

    public final String toString()
    {
        return "datapoints=" + this.dataPoints +
               "  sumX=" + this.sumX +
               "  sumXSq=" + this.sumXSq +
               "  sumY=" + this.sumY +
               "  sumYSq=" + this.sumYSq +
               "  sumXY=" + this.sumXY;
    }

    /**
     * Sets the sum X.
     * @param sumX to set
     */
    public void setSumX(BigDecimal sumX)
    {
        this.sumX = sumX;
    }

    /**
     * Sets the sum X square.
     * @param sumXSq to set
     */
    public void setSumXSq(BigDecimal sumXSq)
    {
        this.sumXSq = sumXSq;
    }

    /**
     * Sets the sum Y.
     * @param sumY to set
     */
    public void setSumY(BigDecimal sumY)
    {
        this.sumY = sumY;
    }

    /**
     * Sets the sum Y square.
     * @param sumYSq to set
     */
    public void setSumYSq(BigDecimal sumYSq)
    {
        this.sumYSq = sumYSq;
    }

    /**
     * Sets the sum of x times y.
     * @param sumXY sum of x times y.
     */
    public void setSumXY(BigDecimal sumXY)
    {
        this.sumXY = sumXY;
    }

    /**
     * Sets the number of datapoints
     * @param dataPoints to set
     */
    public void setDataPoints(long dataPoints)
    {
        this.dataPoints = dataPoints;
    }

    /**
     * Returns sum of x.
     * @return sum of x
     */
    public BigDecimal getSumX()
    {
        return sumX;
    }

    /**
     * Returns sum of y.
     * @return sum of y
     */
    public BigDecimal getSumY()
    {
        return sumY;
    }

    /**
     * Returns the number of datapoints.
     * @return datapoints
     */
    public long getDataPoints()
    {
        return dataPoints;
    }
}
