
package com.omniti.reconnoiter.event;

/**
 * Numeric Metric that carries an extra int field
 *
 * Created by hartmann on 12/15/15.
 */
public class NoitMetricTransformed extends NoitMetric {
    /*
    'MT' REMOTE TIMESTAMP UUID NAME TYPE VALUE ORDERING_ID
    */
    public NoitMetricTransformed() {}
    public NoitMetricTransformed(String[] parts) throws Exception {
        super(parts);
        try {
            orderingId = Integer.valueOf(parts[7]);
        } catch (NumberFormatException nfe) {
            throw new Exception("Error parsing orderingId", nfe);
        }
    }

    // public methods
    @Override
    public String getPrefix() {
        return "MT"; // for M 'Transformed'
    }
    @Override
    public int numparts() { return 8; }
}
