
package com.omniti.reconnoiter.event;

/**
 * Numeric Metric that carries an extra int field
 *
 * Created by hartmann on 12/15/15.
 */
public class NoitMetricTransformed extends NoitMetric {
    public static final String PREFIX = "MT";

    /*
    'MT' REMOTE TIMESTAMP UUID NAME TYPE VALUE ORDERING_ID
    */
    public NoitMetricTransformed() {}
    public NoitMetricTransformed(String[] parts) throws Exception {
        super(parts);
        try {
            orderingId = Integer.valueOf(parts[7]);
            if (nmn != null) { nmn.orderingId = orderingId; }
            if (nmt != null) { nmt.orderingId = orderingId; }
        } catch (NumberFormatException nfe) {
            throw new Exception("Error parsing orderingId", nfe);
        }

    }

    // public methods
    @Override
    public String getPrefix() {
        return PREFIX; // for M 'Transformed'
    }
    @Override
    public int numparts() { return 8; }
}
