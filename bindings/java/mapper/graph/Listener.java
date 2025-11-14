
package mapper.graph;

import mapper.object.Event;

public class Listener<T extends mapper.Object> {
    public void onEvent(mapper.Device device, Event event) {};
    public void onEvent(mapper.Signal signal, Event event) {};
    public void onEvent(mapper.Map map, Event event) {};

}
