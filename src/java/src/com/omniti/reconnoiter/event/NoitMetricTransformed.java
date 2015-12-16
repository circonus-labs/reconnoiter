
package com.omniti.reconnoiter.event;

/**
 * Numeric Metric that carries an extra int field
 *
 * Created by hartmann on 12/15/15.
 */
public class NoitMetricTransformed extends NoitMetric {
    private int orderingId;

    /*
    'MT' REMOTE TIMESTAMP UUID NAME TYPE VALUE ORDERING_ID
    */
    public NoitMetricTransformed() {}
    public NoitMetricTransformed(String[] parts) throws Exception {
        super(parts);
        try {
            orderingId = Integer.valueOf(parts[7]);
        } catch (NumberFormatException nfe) {
            orderingId = -1;
        }
    }

    // public methods
    @Override
    public String getPrefix() {
        return "MT"; // for M 'Transformed'
    }
    @Override
    public int numparts() { return 8; }
    public int getOrderingId() { return orderingId; }
}
