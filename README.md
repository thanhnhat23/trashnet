# trashnet
Code (only for the convolutional neural network) and dataset for mine and [Mindy Yang](http://github.com/yangmindy4)'s final project for [Stanford's CS 229: Machine Learning class](http://cs229.stanford.edu). Our paper can be found [here](https://cs229.stanford.edu/proj2016/report/ThungYang-ClassificationOfTrashForRecyclabilityStatus-report.pdf). The convolutional neural network results on the poster are dated since we continued working after the end of the quarter and  were able to achieve around 75% test accuracy (with 70/13/17 train/val/test split) after changing the weight initialization to the Kaiming method.

## Dataset
This repository contains the dataset that we collected. The dataset spans six classes: glass, paper, cardboard, plastic, metal, and trash. Currently, the dataset consists of 2527 images:
- 501 glass
- 594 paper
- 403 cardboard
- 482 plastic
- 410 metal
- 137 trash

The pictures were taken by placing the object on a white posterboard and using sunlight and/or room lighting. The pictures have been resized down to 512 x 384, which can be changed in `data/constants.py` (resizing them involves going through step 1 in usage). The devices used were Apple iPhone 7 Plus, Apple iPhone 5S, and Apple iPhone SE.

The size of the original dataset, ~3.5GB, exceeds the git-lfs maximum size so it has been uploaded to Google Drive. If you are planning on using the Python code to preprocess the original dataset, then download `dataset-original.zip` from the link below and place the unzipped folder inside of the `data` folder.

**If you are using the dataset, please give a citation of this repository. The dataset can be downloaded [here](https://huggingface.co/datasets/garythung/trashnet).**

## Installation
### Lua setup
We wrote code in [Lua](http://lua.org) using [Torch](http://torch.ch); you can find installation instructions
[here](http://torch.ch/docs/getting-started.html). You'll need the following Lua packages:

- [torch/torch7](http://github.com/torch/torch7)
- [torch/nn](http://github.com/torch/nn)
- [torch/optim](http://github.com/torch/optim)
- [torch/image](http://github.com/torch/image)
- [torch/gnuplot](http://github.com/torch/gnuplot)

After installing Torch, you can install these packages by running the following:

```bash
# Install using Luarocks
luarocks install torch
luarocks install nn
luarocks install optim
luarocks install image
luarocks install gnuplot
```

We also need [@e-lab](http://github.com/e-lab)'s [weight-init module](http://github.com/e-lab/torch-toolbox/blob/master/Weight-init/weight-init.lua), which is already included in this repository.

### CUDA support
Because training takes awhile, you will want to use a GPU to get results in a reasonable amount of time. We used CUDA with a GTX 650 Ti with CUDA. To enable GPU acceleration with CUDA, you'll first need to install CUDA 6.5 or higher. Find CUDA installations [here](http://developer.nvidia.com/cuda-downloads).

Then you need to install following Lua packages for CUDA:
- [torch/cutorch](http://github.com/torch/cutorch)
- [torch/cunn](http://github.com/torch/cunn)

You can install these packages by running the following:

```bash
luarocks install cutorch
luarocks install cunn
```

### Python setup
Python is used for image preprocessing, model training, and inference. The Python dependencies are:
- [PyTorch](https://pytorch.org)
- [TorchVision](https://pytorch.org/vision/stable/index.html)
- [Pillow](https://python-pillow.org)

You can install these packages by running the following:

```bash
# Install using pip
python -m pip install torch torchvision pillow
```

## Usage

### Step 1: Prepare the data
The repository already contains the processed images in `data/dataset-resized`. If this folder is present, this step can be skipped.

To preprocess the original images, place them in the following folders:

```text
data/dataset-original/
	cardboard/
	glass/
	metal/
	paper/
	plastic/
	trash/
```

Then run the following commands from the repository root:

```bash
python -m pip install torch torchvision pillow
python data/resize.py
```

If the dataset is changed, remove `data/dataset-resized` before running the resize script again. The script resizes the images to `512 x 384`, as configured in `data/constants.py`.

### Step 2: Train the model
Run training from the repository root:

```bash
python data/train.py
```

The script uses a fixed 80/20 train-validation split and automatically uses CUDA when it is available. After training, the best model is saved to `data/trashnet_resnet18_best.pth`, and the class mapping is saved to `data/classes.json`.

### Step 3: Test the model
Open `data/predict.py` and set `test_img` to the path of the image to classify:

```python
test_img = r"C:\path\to\image.jpg"
```

Then run:

```bash
python data/predict.py
```

The script loads `data/trashnet_resnet18_best.pth` and prints the predicted class and confidence score. Train the model first so that the checkpoint and `classes.json` are available.

### Step 4: View the results
Training progress is printed in the terminal after every epoch. A prediction is also printed in the terminal in the following format:

```text
File: image.jpg
Result: PLASTIC (Confidence: 92.35%)
```

The trained model and label mapping can be found in the `data` directory. The current Python training script does not export loss or accuracy plots; `plot.lua` is for the legacy Torch checkpoints and cannot load the PyTorch `.pth` file.

## Contributing
1. Fork it!
2. Create your feature branch: `git checkout -b my-new-feature`
3. Commit your changes: `git commit -m 'Add some feature'`
4. Push to the branch: `git push origin my-new-feature`
5. Submit a pull request

## Acknowledgments
- Thanks to the Stanford CS 229 autumn 2016-2017 teaching staff for a great class!
- [@e-lab](http://github.com/e-lab) for their [weight-init Torch module](http://github.com/e-lab/torch-toolbox/blob/master/Weight-init/weight-init.lua)

## TODOs
- add specific results (and parameters used) that were achieved after the CS 229 project deadline
- add saving of confusion matrix data and creation of graphic to `plot.lua`
- rewrite the data preprocessing to only reprocess new images if the dimensions have not changed
