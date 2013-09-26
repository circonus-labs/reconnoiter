/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.esper;

import java.io.Serializable;

/**
 * Bean for performing statistical calculations. The bean keeps sums of X and Y datapoints and sums on squares
 * that can be reused by subclasses. The bean calculates standard deviation (sample and population), variance,
 * average and sum.
  */
public class WeightedValueBean implements Cloneable, Serializable
{
    private long weight;
    private double value;
    private static final long serialVersionUID = 7995123761256377184L;

    public WeightedValueBean(long w, double v) {
        weight = w;
        value = v;
    }

    public long getWeight() { return weight; }
    public double getValue() { return value; }
    public void setWeight(long w) { weight = w; }
    public void setValue(double v) { value = v; }
}
