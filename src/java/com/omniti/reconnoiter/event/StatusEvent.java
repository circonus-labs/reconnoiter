package com.omniti.reconnoiter.event;

public class StatusEvent {
    private String uuid;
    private String status;
    private String available;
    private String state;
    private int duration;

    public StatusEvent(String uuid, String status, String available, String state, int duration) {
        this.uuid = uuid;
        this.status = status;
        this.available = available;
        this.state = state;
        this.duration = duration;
    }

    public String getUuid() {
        return uuid;
    }

    public String getStatus() {
        return status;
    }

    public String getAvailable() {
        return available;
    }

    public String getState() {
        return state;
    }

    public double getDuration() {
        return duration; 
    }
}
