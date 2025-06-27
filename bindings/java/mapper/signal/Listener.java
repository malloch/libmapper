
package mapper.signal;

import mapper.Signal;

public class Listener {
    // singleton
    public void onEvent(Signal signal, mapper.object.Status event, float value, mapper.Time time) {};
    public void onEvent(Signal signal, mapper.object.Status event, int value, mapper.Time time) {};
    public void onEvent(Signal signal, mapper.object.Status event, double value, mapper.Time time) {};
    public void onEvent(Signal signal, mapper.object.Status event, float[] value, mapper.Time time) {};
    public void onEvent(Signal signal, mapper.object.Status event, int[] value, mapper.Time timw) {};
    public void onEvent(Signal signal, mapper.object.Status event, double[] value, mapper.Time time) {};

    // instanced
    public void onEvent(Signal.Instance signal, mapper.object.Status event, float value, mapper.Time time) {};
    public void onEvent(Signal.Instance signal, mapper.object.Status event, int value, mapper.Time time) {};
    public void onEvent(Signal.Instance signal, mapper.object.Status event, double value, mapper.Time time) {};
    public void onEvent(Signal.Instance signal, mapper.object.Status event, float[] value, mapper.Time time) {};
    public void onEvent(Signal.Instance signal, mapper.object.Status event, int[] value, mapper.Time time) {};
    public void onEvent(Signal.Instance signal, mapper.object.Status event, double[] value, mapper.Time time) {};
}
