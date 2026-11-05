from PIL import Image

from fusion.predictor import predict


IMAGE_PATH = "test_yolo/test.png"


print("=" * 60)
print("TRASHNET - SOFT VOTING FUSION TEST")
print("=" * 60)

image = Image.open(IMAGE_PATH)

result = predict(image)

print()
print("Image:", IMAGE_PATH)

print()
print("----- RESNET18 -----")
print("Prediction:", result["resnet_prediction"])
print("Confidence:", result["resnet_confidence"], "%")

print()
print("----- YOLO11n-CLS -----")
print("Prediction:", result["yolo_prediction"])
print("Confidence:", result["yolo_confidence"], "%")

print()
print("----- SOFT VOTING -----")
print(
    "Formula: "
    f"{result['resnet_weight']} * ResNet + "
    f"{result['yolo_weight']} * YOLO"
)

print("Final prediction:", result["class"])
print("Final confidence:", result["confidence"], "%")

print()
print("----- ALL FUSION PROBABILITIES -----")

for item in result["all_predictions"]:
    print(
        f"{item['class']:12s}: "
        f"{item['confidence']:.2f}%"
    )

print("=" * 60)
