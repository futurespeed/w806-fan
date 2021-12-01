import cv2

def toBit(path):
    img = cv2.imread(path)
    print("width: %d, height: %d" % (len(img[0]),len(img)))
    
    out_img = []
    for img_y in img:
        tmp_y = []
        for img_x in img_y:
            b = 0
            if(img_x[0] > 128): b = 1
            tmp_y.append(b)
        out_img.append(tmp_y)

    print("static const uint8_t img[] = {")
    for y in out_img:
        for x in y:
            print("0x0%d" % x, end=",")
        print("")
    print("};")
    


toBit('./fan-icon.png')