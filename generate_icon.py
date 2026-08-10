"""将 scissors.png 转换为 scissors.ico（多种尺寸）"""
from PIL import Image

img = Image.open('scissors.png')
sizes = [256, 128, 64, 48, 32, 16]
max_size = max(img.size)
sizes = [s for s in sizes if s <= max_size] or [max_size]
img.save('scissors.ico', format='ICO', sizes=[(s, s) for s in sizes])
print(f'✅ scissors.ico 生成成功！包含尺寸: {sizes}')
