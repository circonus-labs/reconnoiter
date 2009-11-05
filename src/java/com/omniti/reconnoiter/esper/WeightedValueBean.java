package com.omniti.reconnoiter.esper;

import java.io.Serializable;
import java.math.BigDecimal;

/**
 * Bean for performing statistical calculations. The bean keeps sums of X and Y datapoints and sums on squares
 * that can be reused by subclasses. The bean calculates standard deviation (sample and population), variance,
 * average and sum.
  */
public class WeightedValueBean implements Cloneable, Serializable
{
    private double weight;
    private double value;
    private static final long serialVersionUID = 7995123761256377184L;

    public WeightedValueBean(BigDecimal w, BigDecimal v) {
        weight = (w == null) ? Double.NaN : w.doubleValue();
        value = (v == null) ? Double.NaN: v.doubleValue();
    }

    public double getWeight() { return weight; }
    public double getValue() { return value; }
    public void setWeight(double w) { weight = w; }
    public void setValue(double v) { value = v; }
}
