#include <iostream>
#include <cstdlib>
#include <cmath>
#include <random>

#include <stb/stb_image.h>

#include <glm/glm.hpp>
#include <ctime>
#include "gl_includes.h"
#include "Perf.h"

using namespace std;
using namespace glm;


// ---------------------------- Rendering (Globals) ---------------------------

#define GLSL(src) "#version 400\n" #src
void checkShaderError(GLuint shader);
void checkLinkError(GLuint program);
void loadTexture(GLuint texture, const char *filename);

GLFWwindow* window;

const char *vert = GLSL(
    uniform float invwidth;
    uniform float invheight;
    uniform int offsetX;
    uniform int offsetY;
    in vec2 pos;
    out vec2 texCoord;

    void main() {
        vec2 pixelPos = (vec2(offsetX, offsetY) + pos) * 16;
        gl_Position = vec4(pixelPos.x * invwidth, -pixelPos.y * invheight, 0.0, 1.0);
        texCoord = pos;
    }
);
const char *frag = GLSL(
    uniform sampler2D tex;

    in vec2 texCoord;

    void main() {
        gl_FragColor = texture(tex, texCoord);
    }
);

struct {
    int invwidth;
    int invheight;
    int offsetX;
    int offsetY;
    int tex;
} uniforms;



// -------------------------- Game Logic --------------------------

const int nMines = 150;
const int width = 30;
const int height = 30;
const int nCells = width * height;

enum CellType {
    // Numerical values are their ordinal
    ZERO = 0,
    ONE = 1,
    TWO = 2,
    THREE = 3,
    FOUR = 4,
    FIVE = 5,
    SIX = 6,
    SEVEN = 7,
    EIGHT = 8,

    // Special values come after.
    MINE = 9,
    FLAG = 10,
    UNKNOWN = 11,
};

int cells[nCells]; // displayed to the user
int values[nCells]; // true game data
char visited[nCells]; // flags for the expand(..) function
int nCorrectFlags;
int nIncorrectFlags;

enum {
    UNINITIALIZED,
    RUNNING,
    GAME_OVER,
} state;

template<typename L>
inline void visitNeighbors(int index, L accept) {
    int y = index / width;
    int x = index % width;

    bool left = x > 0;
    bool right = x < width-1;
    bool top = y > 0;
    bool bot = y < height-1;

    if (left && top)  accept(index - width - 1);
    if (left)         accept(index         - 1);
    if (left && bot)  accept(index + width - 1);
    if (bot)          accept(index + width    );
    if (right && bot) accept(index + width + 1);
    if (right)        accept(index         + 1);
    if (right && top) accept(index - width + 1);
    if (top)          accept(index - width    );
}

void tryIncrement(int index) {
    if (values[index] < 8) {
        values[index]++;
    }
}

mt19937 mt(clock()); // random_device is deterministic when compiled with mingw, so use clock() instead.
uniform_int_distribution<int> randCell(0, nCells-1); // [inclusive, inclusive]

void initGame() {
    for (int c = 0; c < nCells; c++) {
        cells[c] = UNKNOWN;
        values[c] = ZERO;
    }

    state = UNINITIALIZED;
}

void initBoard(int firstClickIndex) {
    for (int c = 0; c < nMines;) {
        int index = randCell(mt);
        if (index != firstClickIndex && values[index] != MINE) {
            values[index] = MINE;
            visitNeighbors(index, tryIncrement);
            c++;
        }
    }

    nCorrectFlags = 0;
    nIncorrectFlags = 0;

    state = RUNNING;
}

void gameOver() {
    for (int c = 0; c < nCells; c++) {
        if (cells[c] != FLAG || values[c] != MINE) {
            cells[c] = values[c];
        }
    }
    state = GAME_OVER;
}

void expandCellInner(int index) {
    if (!visited[index]) {
        visited[index] = 1;
        cells[index] = values[index];
        if (values[index] == ZERO) {
            visitNeighbors(index, expandCellInner);
        }
    }
}

void expandCell(int index) {
    memset(visited, 0, size_t(nCells));
    visited[index] = 1;
    visitNeighbors(index, expandCellInner);
}

void openCell(int index) {
    int value = values[index];
    cells[index] = value;
    switch (value) {
        case MINE:
            gameOver();
            break;
        case ZERO:
            expandCell(index);
            break;
        default:
            // do nothing
            break;
    }
}

void tryOpenCell(int index) {
    if (cells[index] == UNKNOWN) // don't open flagged cells
        openCell(index);
}

void openAll(int index) {
    // only do this if we're on a known cell.
    if (cells[index] == UNKNOWN)
        return;

    // count neighboring flags
    int neighborFlags = 0;
    visitNeighbors(index, [&neighborFlags](int neighbor) {
        if (cells[neighbor] == FLAG) neighborFlags++;
    });

    // only open all unflagged neighbors if the number of flags is correct.
    if (neighborFlags == cells[index]) {
        visitNeighbors(index, tryOpenCell);
    }
}

void flagCell(int index) {
    switch (cells[index]) {
        case UNKNOWN:
            cells[index] = FLAG;
            if (values[index] == MINE) nCorrectFlags++;
            else nIncorrectFlags++;
            break;
        case FLAG:
            cells[index] = UNKNOWN;
            if (values[index] == MINE) nCorrectFlags--;
            else nIncorrectFlags--;
            break;
        default:
            // do nothing
            break;
    }

    if (nCorrectFlags == nMines && nIncorrectFlags == 0) {
        gameOver(); // technically win, but equivalent.
    }
}


// -------------------------- UI and Control ------------------------

const float scale = 4;
const int cellWidthRaw = 8; // width of the cell sprite, in px
const int cellWidth = (int) (cellWidthRaw * scale); // width after scaling

int getCellIndex(double xf, double yf) {
    int x = (int) xf;
    int y = (int) yf;

    // x and y are in pixels from top-left corner
    int sw, sh;
    glfwGetWindowSize(window, &sw, &sh);

    // (cx, cy) is relative to (0, 0)
    int cx = x - sw/2;
    int cy = y - sh/2;

    int cell_cx = cx / cellWidth;
    int cell_cy = cy / cellWidth;

    int x_00 = sw/2 - width/2 * cellWidth;
    int y_00 = sh/2 - height/2 * cellWidth;

    int board_x = x - x_00;
    int board_y = y - y_00;

    if (board_x < 0 || board_y < 0) return -1;
    int cell_x = board_x / cellWidth;
    int cell_y = board_y / cellWidth;

    if (cell_x >= width || cell_y >= height) return -1;

    return cell_x + cell_y * width;
}

void resize_callback(GLFWwindow *window, int width, int height) {
    cout << "Size " << width << " by " << height << endl;
    glViewport(0, 0, width, height);
    if (width != 0) glUniform1f(uniforms.invwidth, scale / width);
    if (height != 0) glUniform1f(uniforms.invheight, scale / height);
}

void setup() {
    // setup shaders
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vert, nullptr);
    glCompileShader(vertex);
    checkShaderError(vertex);

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &frag, nullptr);
    glCompileShader(fragment);
    checkShaderError(fragment);

    GLuint shader = glCreateProgram();
    glAttachShader(shader, vertex);
    glAttachShader(shader, fragment);
    glLinkProgram(shader);
    checkLinkError(shader);

    uniforms.invwidth = glGetUniformLocation(shader, "invwidth");
    uniforms.invheight = glGetUniformLocation(shader, "invheight");
    uniforms.offsetX = glGetUniformLocation(shader, "offsetX");
    uniforms.offsetY = glGetUniformLocation(shader, "offsetY");
    uniforms.tex = glGetUniformLocation(shader, "tex");

    GLuint posLocation = glGetAttribLocation(shader, "pos");

    glUseProgram(shader);

    // setup mesh
    float points[] = {
            0,0,
            1,0,
            0,1,
            1,1
    };

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);

    glEnableVertexAttribArray(posLocation);
    glVertexAttribPointer(posLocation, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), 0);
    checkError();

    GLuint textures[12];
    glGenTextures(12, textures);

    glActiveTexture(GL_TEXTURE0);
    loadTexture(textures[0], "assets/0.png");
    glActiveTexture(GL_TEXTURE1);
    loadTexture(textures[1], "assets/1.png");
    glActiveTexture(GL_TEXTURE2);
    loadTexture(textures[2], "assets/2.png");
    glActiveTexture(GL_TEXTURE3);
    loadTexture(textures[3], "assets/3.png");
    glActiveTexture(GL_TEXTURE4);
    loadTexture(textures[4], "assets/4.png");
    glActiveTexture(GL_TEXTURE5);
    loadTexture(textures[5], "assets/5.png");
    glActiveTexture(GL_TEXTURE6);
    loadTexture(textures[6], "assets/6.png");
    glActiveTexture(GL_TEXTURE7);
    loadTexture(textures[7], "assets/7.png");
    glActiveTexture(GL_TEXTURE8);
    loadTexture(textures[8], "assets/8.png");
    glActiveTexture(GL_TEXTURE9);
    loadTexture(textures[MINE], "assets/mine.png");
    glActiveTexture(GL_TEXTURE10);
    loadTexture(textures[FLAG], "assets/flag.png");
    glActiveTexture(GL_TEXTURE11);
    loadTexture(textures[UNKNOWN], "assets/unknown.png");

    glfwSetFramebufferSizeCallback(window, resize_callback); // must happen after shader setup
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    resize_callback(window, width, height);

    initGame();
}

void draw() {
    int index = 0;
    for (int y = 0; y < height; y++) {
        glUniform1i(uniforms.offsetY, y - height / 2);
        checkError();
        for (int x = 0; x < width; x++) {
            glUniform1i(uniforms.offsetX, x - width / 2);
            checkError();
            glUniform1i(uniforms.tex, cells[index]);
            if (checkError()) {
                cout << cells[index] << endl;
            }
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            checkError();
            index++;
        }
    }
}

static void glfw_click_callback(GLFWwindow *window, int button, int action, int mods) {
    if (action != GLFW_RELEASE) return;

    if (state == GAME_OVER) {
        initGame();
        return;
    }

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    int cellIndex = getCellIndex(x, y);
    if (cellIndex < 0) return;

    if (state == UNINITIALIZED) {
        initBoard(cellIndex);
        // don't return, continue with click
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        flagCell(cellIndex);
    } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
        openCell(cellIndex);
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        openAll(cellIndex);
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, true);
    } else if (key == GLFW_KEY_W) {
        static bool wireframe = false;
        wireframe = !wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    }
}

void glfw_error_callback(int error, const char* description) {
    cerr << "GLFW Error: " << description << " (error " << error << ")" << endl;
}

void checkShaderError(GLuint shader) {
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success) return;

    cout << "Shader Compile Failed." << endl;

    GLint logSize = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
    if (logSize == 0) {
        cout << "No log found." << endl;
        return;
    }

    GLchar *log = new GLchar[logSize];

    glGetShaderInfoLog(shader, logSize, &logSize, log);

    cout << log << endl;

    delete[] log;
}

void checkLinkError(GLuint program) {
    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success) return;

    cout << "Shader link failed." << endl;

    GLint logSize = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logSize);
    if (logSize == 0) {
        cout << "No log found." << endl;
        return;
    }

    GLchar *log = new GLchar[logSize];

    glGetProgramInfoLog(program, logSize, &logSize, log);
    cout << log << endl;

    delete[] log;
}

void loadTexture(GLuint texname, const char *filename) {
    glBindTexture(GL_TEXTURE_2D, texname);

    int width, height, bpp;
    unsigned char *pixels = stbi_load(filename, &width, &height, &bpp, STBI_default);
    if (pixels == nullptr) {
        cout << "Failed to load image " << filename << " (" << stbi_failure_reason() << ")" << endl;
        return;
    }
    cout << "Loaded " << filename << ", " << height << 'x' << width << ", comp = " << bpp << endl;

    GLenum format;
    switch(bpp) {
        case STBI_rgb:
            format = GL_RGB;
            break;
        case STBI_rgb_alpha:
            format = GL_RGBA;
            break;
        default:
            cout << "Unsupported format: " << bpp << endl;
            return;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(pixels);
}

int main() {
    if (!glfwInit()) {
        cout << "Failed to init GLFW" << endl;
        exit(-1);
    }
    cout << "GLFW Successfully Started" << endl;

    glfwSetErrorCallback(glfw_error_callback);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window = glfwCreateWindow(width * cellWidth, height * cellWidth, "One Night Ultimate Minesweeper", NULL, NULL);
    if (!window) {
        cout << "Failed to create window" << endl;
        exit(-1);
    }
    glfwSetKeyCallback(window, glfw_key_callback);
    glfwSetMouseButtonCallback(window, glfw_click_callback);

    glfwMakeContextCurrent(window);
    glewInit();

    glfwSwapInterval(1);

    initPerformanceData();

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    setup();
    checkError();

    // make sure performance data is clean going into main loop
    markPerformanceFrame();
    printPerformanceData();
    double lastPerfPrintTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {

        {
            Perf stat("Draw");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            draw();
            checkError();
        }

        {
            Perf stat("Swap buffers");
            glfwSwapBuffers(window);
            checkError();
        }
        {
            Perf stat("Poll events");
            glfwPollEvents();
            checkError();
        }

        markPerformanceFrame();

        double now = glfwGetTime();
        if (now - lastPerfPrintTime > 10.0) {
            printPerformanceData();
            lastPerfPrintTime = now;
        }
    }

    return 0;
}
