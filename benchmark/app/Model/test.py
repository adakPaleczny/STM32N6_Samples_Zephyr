import tensorflow as tf

# Load TFLite model
# interpreter = tf.lite.Interpreter(model_path="quantized_tiny_yolo_v2_224_.tflite")
# [{'name': 'serving_default_input_1:0', 'index': 0, 'shape': array([  1, 224, 224,   3], dtype=int32), 
# 'shape_signature': array([ -1, 224, 224,   3], dtype=int32),
#  'dtype': <class 'numpy.uint8'>, 'quantization': (0.003921568859368563, 0), 
# 'quantization_parameters': {'scales': array([0.00392157], dtype=float32), 
# 'zero_points': array([0], dtype=int32), 'quantized_dimension': 0}, 'sparsity_parameters': {}}]

interpreter = tf.lite.Interpreter(model_path="yolov2t_224_int8.tflite")
# [{'name': 'serving_default_input_1:0', 'index': 0, 'shape': array([  1, 224, 224,   3], dtype=int32), 
# 'shape_signature': array([ -1, 224, 224,   3], dtype=int32), 
# 'dtype': <class 'numpy.uint8'>, 'quantization': (0.007843137718737125, 127), 
# 'quantization_parameters': {'scales': array([0.00784314], dtype=float32), 
# 'zero_points': array([127], dtype=int32), 'quantized_dimension': 0}, 'sparsity_parameters': {}}]
interpreter.allocate_tensors()

# Get input details
input_details = interpreter.get_input_details()
print(input_details)