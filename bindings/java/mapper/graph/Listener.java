
package mapper.graph;

public class Listener<T extends mapper.Object> {
    public void onEvent(mapper.Device device, mapper.object.Status event) {};
    public void onEvent(mapper.Signal signal, mapper.object.Status event) {};
    public void onEvent(mapper.Map map, mapper.object.Status event) {};

}
