This is a prototype/demo for a video image telemetry system for linux.

I want to know how efficient or costly it is to use protobufs to carry images on a zeromq socket.

How many image memory copies happen, what is the minimum latency we can acheive?

There is a producer service that runs on linux and a python gui service that runs on a windows-pc to display images and telemetry data.

We will write a custom V4L2 synthetic camera, a protobuf wrapper, a linux service-main, and a seperate
python gui tool.

Initially, the V4L2-synthetic camera will not be a true V4L2 linux module, just a class that looks like a camera. Bonus points: phase-2 implement this as an actual V4L2 camera in the linux kernel.

The producer has a main which owns an instance of a V4L2 synthetic camera and a telemetry object
and can wrap images in protobuf messages.

The producer main contains a V4L2 synthetic camera that creates 30 fps of 800x600 pixel images.

The images are YUV grayscale.

Each image is white background with black letters representing the milliseconds since 1970.

The producer main wraps the images in a protobuf message with instrumentation. 

The producer main drops the PB message into a telemtry object.

The telemetry object is a c++ class that accepts PB and queues them.

The telemetry object has its own service thread. The image input queue must be thread safe.

The telemtry object drops the image PB into a zeromq publish socket.

The telemtry object is intrumented to measure the queue depth.

The telemetry object has a second zeromq publish socket that publishes a text string indicating
the min,max, and mean time objects are in the queue. The telemtry output should also indicate
the number of image copies per frame, the CPU and memory usage of the camera service, and the total microseconds per frame of execution time.

The python GUI tool should connect to the producer source, receive the PB messages from the zeromq socket,
display the images and the telemtry data in real-time. The user should see the milliseconds-since-1970
counter incrementing at 30 fps.

The project should have a docs/ folder with (at a minimum) requirements, design, and test-plan documents.
