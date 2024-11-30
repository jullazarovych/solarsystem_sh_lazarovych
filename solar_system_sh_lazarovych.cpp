#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const unsigned int SCR_WIDTH = 1024;
const unsigned int SCR_HEIGHT = 1024;  

const float PI = 3.14159265359f;
float day = 0.0f;
glm::vec3 cameraPos = glm::vec3(0.0f, 1.0f, 10.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = -90.0f; 
float pitch = 0.0f;  
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float fov = 45.0f; 

size_t skyIndexCount;
GLuint skyTextureID;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

struct Material {
    glm::vec3 ambient;//фон.осв планети
    glm::vec3 specular;//альбедо
    float shininess; //шереховатість
    glm::vec3 emission; //власне світло
};

struct CelestialBody {
    float orbitRadius;      
    float orbitSpeed;      
    float rotationSpeed;   
    float size;
    float rotationDirection;
    float axisTilt; 
    Material material; 
    std::string texturePath;
    GLuint textureID;
};
std::vector<CelestialBody> celestialBodies;

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

out vec2 TexCoord;
out vec3 FragPos; 
out vec3 Normal; 

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
   vec4 worldPosition = model * vec4(aPos, 1.0);
    FragPos = vec3(worldPosition); 
    Normal = mat3(transpose(inverse(model))) * aNormal; 
    TexCoord = aTexCoord; 
    gl_Position = projection * view * worldPosition; 
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
struct Material {
    sampler2D texture_diffuse;
    vec3 specular;
    float shininess;
    vec3 emission;
};

struct Light {
    vec3 position;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 viewPos;
uniform Material material;
uniform Light light;
uniform int isSkybox; 

void main() {
    if (isSkybox == 1) {
        vec3 color = texture(material.texture_diffuse, TexCoord).rgb;
        FragColor = vec4(color, 1.0);
    } else {
        vec3 diffuseMap = texture(material.texture_diffuse, TexCoord).rgb;
        vec3 ambient = light.ambient * diffuseMap;
        vec3 norm = normalize(Normal);
        vec3 lightDir = normalize(light.position - FragPos);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = light.diffuse * diff * diffuseMap;
        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
        vec3 specular = light.specular * (spec * material.specular);
        vec3 emission = material.emission * diffuseMap;
        vec3 result = ambient + diffuse + specular + emission;
        FragColor = vec4(result, 1.0);
    }
}
)";

GLuint loadTexture(const std::string& texturePath){
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(texturePath.c_str(), &width, &height, &nrChannels, 0);
    if (data){
        GLenum format;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        std::cout << "Not found: " << texturePath << std::endl;
    }
    stbi_image_free(data);
    return textureID;
}

void generateSphere(std::vector<float>& vertices, std::vector<unsigned int>& indices, float radius, unsigned int sectorCount, unsigned int stackCount, bool ifNotSky){
    float x, y, z, xy;                             
    float nx, ny, nz, lengthInv = 1.0f / radius;    
    float s, t;                                     

    float sectorStep = 2 * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;
    
    float sectorAngle, stackAngle;
    if (ifNotSky) {
        for (unsigned int i = 0; i <= stackCount; ++i){
            stackAngle = M_PI / 2 - i * stackStep;        // від π/2 до -π/2
            xy = radius * cosf(stackAngle);             
            y = radius * sinf(stackAngle);              

            for (unsigned int j = 0; j <= sectorCount; ++j){
                sectorAngle = j * sectorStep;   
                x = xy * cosf(sectorAngle);             
                z = xy * sinf(sectorAngle);            
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);

                s = (float)j / sectorCount;
                t = (float)i / stackCount;
                vertices.push_back(s);
                vertices.push_back(t);
                nx = x * lengthInv;
                ny = y * lengthInv;
                nz = z * lengthInv;
                vertices.push_back(nx);
                vertices.push_back(ny);
                vertices.push_back(nz);
            }
        }
    }
    else{
        for (unsigned int i = 0; i <= stackCount; ++i){
            stackAngle = M_PI / 2 - i * stackStep;
            xy = radius * cosf(stackAngle);
            y = radius * sinf(stackAngle);

            for (unsigned int j = 0; j <= sectorCount; ++j){
                sectorAngle = j * sectorStep;
                x = xy * cosf(sectorAngle);
                z = xy * sinf(sectorAngle);
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
                nx = x * lengthInv;
                ny = y * lengthInv;
                nz = z * lengthInv;
                vertices.push_back(nx);
                vertices.push_back(ny);
                vertices.push_back(nz);
                s = sectorAngle / (2 * M_PI);
                t = stackAngle / M_PI + 0.5f;
                vertices.push_back(s);
                vertices.push_back(t);
            }
        }
    }
    unsigned int k1, k2;
    for (unsigned int i = 0; i < stackCount; ++i){
        k1 = i * (sectorCount + 1);    
        k2 = k1 + sectorCount + 1;    
        for (unsigned int j = 0; j < sectorCount; ++j, ++k1, ++k2){
            if (i != 0){
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)){
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }
}
void drawSkySphere(GLuint shaderProgram, glm::mat4 view, glm::mat4 projection) {
    static GLuint VAO = 0, VBO = 0, EBO = 0;
    static size_t indexCount = 0;
    static bool initialized = false;

    if (!initialized) {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        float radius = 20.0f; 
        generateSphere(vertices, indices, radius, 36, 18, false);
        skyIndexCount = indices.size();
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        int stride = (3 + 2 + 3) * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
        skyTextureID = loadTexture("D:/vscode_asd_laz/test_shaders/pictures/bg.jpeg");
        initialized = true;
    }

    glDepthMask(GL_FALSE);

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), cameraPos);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyTextureID);
    glUniform1i(glGetUniformLocation(shaderProgram, "material.texture_diffuse"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "isSkybox"), 1);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(skyIndexCount), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}
void drawCelestialBody(GLuint shaderProgram, glm::mat4 view, glm::mat4 projection, glm::vec3 viewPos, const CelestialBody& celestialBody, glm::mat4 parentModel = glm::mat4(1.0f)) {
    static std::unordered_map<float, GLuint> vaoMap;
    static std::unordered_map<float, GLuint> vboMap;
    static std::unordered_map<float, GLuint> eboMap;
    static std::unordered_map<float, size_t> indexCountMap;

    GLuint VAO, VBO, EBO;
    size_t indexCount;
    if (vaoMap.find(celestialBody.size) == vaoMap.end()) {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        generateSphere(vertices, indices, celestialBody.size, 36, 18, true);

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        int stride = (3 + 2 + 3) * sizeof(float); 
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(0));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);

        vaoMap[celestialBody.size] = VAO;
        vboMap[celestialBody.size] = VBO;
        eboMap[celestialBody.size] = EBO;
        indexCountMap[celestialBody.size] = indices.size();
    }
    else {
        VAO = vaoMap[celestialBody.size];
        VBO = vboMap[celestialBody.size];
        EBO = eboMap[celestialBody.size];
        indexCount = indexCountMap[celestialBody.size];
    }
    glm::mat4 model = parentModel;

    float orbitAngle = glm::radians(day * celestialBody.orbitSpeed);
    model = glm::rotate(model, orbitAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::translate(model, glm::vec3(celestialBody.orbitRadius, 0.0f, 0.0f));

    model = glm::rotate(model, glm::radians(celestialBody.axisTilt), glm::vec3(1.0f, 0.0f, 0.0f));

    float selfRotationAngle = glm::radians(day * celestialBody.rotationSpeed * celestialBody.rotationDirection);
    model = glm::rotate(model, selfRotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    glUseProgram(shaderProgram);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, glm::value_ptr(viewPos));

    glUniform3f(glGetUniformLocation(shaderProgram, "light.position"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(shaderProgram, "light.ambient"), 0.2f, 0.2f, 0.2f);
    glUniform3f(glGetUniformLocation(shaderProgram, "light.diffuse"), 0.8f, 0.8f, 0.8f);
    glUniform3f(glGetUniformLocation(shaderProgram, "light.specular"), 1.0f, 1.0f, 1.0f);

    glUniform3fv(glGetUniformLocation(shaderProgram, "material.ambient"), 1, glm::value_ptr(celestialBody.material.ambient));
    glUniform3fv(glGetUniformLocation(shaderProgram, "material.specular"), 1, glm::value_ptr(celestialBody.material.specular));
    glUniform1f(glGetUniformLocation(shaderProgram, "material.shininess"), celestialBody.material.shininess);
    glUniform3fv(glGetUniformLocation(shaderProgram, "material.emission"), 1, glm::value_ptr(celestialBody.material.emission));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, celestialBody.textureID);
    glUniform1i(glGetUniformLocation(shaderProgram, "material.texture_diffuse"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "isSkybox"), 0);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCountMap[celestialBody.size]), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
void initСelestialBodies() {
    celestialBodies.clear();
    CelestialBody mercury;
    mercury.orbitRadius = 0.579f;
    mercury.orbitSpeed = 23.9f;
    mercury.rotationSpeed = 8.8f;
    mercury.size = 0.024397f;
    mercury.rotationDirection = 1;
    mercury.axisTilt = 0.034f;
    mercury.material.ambient = glm::vec3(0.2f, 0.2f, 0.2f);
    mercury.material.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    mercury.material.shininess = 8.0f;
    mercury.material.emission = glm::vec3(0.0f);
    mercury.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/mercury.jpg";
    mercury.textureID = loadTexture(mercury.texturePath);
    celestialBodies.push_back(mercury);

    CelestialBody venus;
    venus.orbitRadius = 0.7f;
    venus.orbitSpeed = 18.0f;
    venus.rotationSpeed = 22.5f;
    venus.size = 0.060518f;
    venus.rotationDirection = -1;
    venus.axisTilt = 177.4f;
    venus.material.ambient = glm::vec3(0.8f, 0.8f, 0.7f);;
    venus.material.specular = glm::vec3(0.4f, 0.4f, 0.4f);
    venus.material.shininess = 50.0f;
    venus.material.emission = glm::vec3(0.0f);
    venus.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/venus.jpg";
    venus.textureID = loadTexture(venus.texturePath);
    celestialBodies.push_back(venus);

    CelestialBody mars;
    mars.orbitRadius = 1.5f;
    mars.orbitSpeed = 12.1f;
    mars.rotationSpeed = 68.7f;
    mars.size = 0.033895f;
    mars.rotationDirection = 1;
    mars.axisTilt = 25.19f;
    mars.material.ambient = glm::vec3(0.3f, 0.1f, 0.1f);
    mars.material.specular = glm::vec3(0.2f, 0.2f, 0.2f);
    mars.material.shininess = 16.0f;
    mars.material.emission = glm::vec3(0.0f);
    mars.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/mars.jpg";
    mars.textureID = loadTexture(mars.texturePath);
    celestialBodies.push_back(mars);

    CelestialBody jupiter;
    jupiter.orbitRadius = 3.0f;
    jupiter.orbitSpeed = 7.1f;
    jupiter.rotationSpeed = 1.6f;
    jupiter.size = 0.69911f;
    jupiter.rotationDirection = 1;
    jupiter.axisTilt = 3.13f;
    jupiter.material.ambient = glm::vec3(0.6f, 0.4f, 0.3f);
    jupiter.material.specular = glm::vec3(0.2f, 0.2f, 0.2f);
    jupiter.material.shininess = 20.0f;
    jupiter.material.emission = glm::vec3(0.0f);
    jupiter.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/jupiter.jpg";
    jupiter.textureID = loadTexture(jupiter.texturePath);
    celestialBodies.push_back(jupiter);

    CelestialBody saturn;
    saturn.orbitRadius = 5.0f;
    saturn.orbitSpeed = 5.7f;
    saturn.rotationSpeed = 3.9f;
    saturn.size = 0.58232f;
    saturn.rotationDirection = 1;
    saturn.axisTilt = 26.7f;
    saturn.material.ambient = glm::vec3(0.7f, 0.6f, 0.5f);
    saturn.material.specular = glm::vec3(0.3f, 0.3f, 0.3f);
    saturn.material.shininess = 23.0f;
    saturn.material.emission = glm::vec3(0.0f);
    saturn.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/saturn.jpg";
    saturn.textureID = loadTexture(saturn.texturePath);
    celestialBodies.push_back(saturn);

    CelestialBody uran;
    uran.orbitRadius = 7.0f;
    uran.orbitSpeed = 3.8f;
    uran.rotationSpeed = 11.2f;
    uran.size = 0.25362f;
    uran.rotationDirection = 1;
    uran.axisTilt = 97.77f;
    uran.material.ambient = glm::vec3(0.1f, 0.3f, 0.8f);
    uran.material.specular = glm::vec3(0.2f, 0.2f, 0.2f);
    uran.material.shininess = 28.0f;
    uran.material.emission = glm::vec3(0.0f);
    uran.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/uranus.jpg";
    uran.textureID = loadTexture(uran.texturePath);
    celestialBodies.push_back(uran);

    CelestialBody neptun;
    neptun.orbitRadius = 8.0f;
    neptun.orbitSpeed = 3.4f;
    neptun.rotationSpeed = 6.0f;
    neptun.size = 0.24622f;
    neptun.rotationDirection = 1;
    neptun.axisTilt = 28.32f;
    neptun.material.ambient = glm::vec3(0.1f, 0.1f, 0.1f);
    neptun.material.specular = glm::vec3(0.5f, 0.5f, 0.5f);
    neptun.material.shininess = 32.0f;
    neptun.material.emission = glm::vec3(0.0f);
    neptun.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/neptun.jpg";
    neptun.textureID = loadTexture(neptun.texturePath);
    celestialBodies.push_back(neptun);
}

void processInput(GLFWwindow* window){
    float cameraSpeed = 2.5f * deltaTime; 

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
        fov -= cameraSpeed * 20.0f; 
    if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
        fov += cameraSpeed * 20.0f;
    if (fov < 1.0f)
        fov = 1.0f;
    if (fov > 90.0f)
        fov = 90.0f;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos){
    if (firstMouse)
    {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float sensitivity = 0.08f; 
    float xoffset = ((float)xpos - lastX) * sensitivity;
    float yoffset = (lastY - (float)ypos) * sensitivity; 

    lastX = (float)xpos;
    lastY = (float)ypos;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height){
    glViewport(0, 0, width, height);
}

int main(){
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Solar System", NULL, NULL);
    if (window == NULL) {
        std::cerr << "Не вдалося створити вікно GLFW" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Не вдалося ініціалізувати GLAD" << std::endl;
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);

    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    initСelestialBodies();
    CelestialBody moon;
    moon.size = 0.027f;
    moon.orbitRadius = 0.3f; 
    moon.orbitSpeed = 13.0f; 
    moon.rotationSpeed = 0.0f; 
    moon.rotationDirection = 1.0f;
    moon.axisTilt = 6.68f;
    moon.material.ambient = glm::vec3(0.2f, 0.2f, 0.2f);;
    moon.material.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    moon.material.shininess = 8.0f;
    moon.material.emission = glm::vec3(0.0f);
    moon.textureID = loadTexture("D:/vscode_asd_laz/test_shaders/pictures/moon.jpg");

    CelestialBody earth;
    earth.orbitRadius = 1.0f;
    earth.orbitSpeed = 15.8f;
    earth.rotationSpeed = 36.5f;
    earth.size = 0.063710f;
    earth.rotationDirection = 1;
    earth.axisTilt = 23.44f;
    earth.material.ambient = glm::vec3(0.1f, 0.1f, 0.1f);
    earth.material.specular = glm::vec3(0.5f, 0.5f, 0.5f);
    earth.material.shininess = 32.0f;
    earth.material.emission = glm::vec3(0.0f); 
    earth.texturePath = "D:/vscode_asd_laz/test_shaders/pictures/terra.jpg";
    earth.textureID = loadTexture(earth.texturePath);
    celestialBodies.push_back(earth);

    CelestialBody sun;
    sun.size = 0.15f;
    sun.orbitRadius = 0.0f; 
    sun.orbitSpeed = 0.0f;
    sun.rotationSpeed = 10.0f;
    sun.rotationDirection = 1.0f;
    sun.axisTilt = 0.0f;
    sun.material.ambient = glm::vec3(0.7f, 0.7f, 0.7f);
    sun.material.specular = glm::vec3(1.0f, 1.0f, 1.0f);
    sun.material.shininess = 20.0f;
    sun.material.emission = glm::vec3(1.0f);
    sun.textureID = loadTexture("D:/vscode_asd_laz/test_shaders/pictures/sun.jpg");

    glEnable(GL_DEPTH_TEST);
   
    glm::mat4 sunModel = glm::mat4(1.0f);

    while (!glfwWindowShouldClose(window)) {

        glm::mat4 earthModel = glm::mat4(1.0f);
        float earthOrbitAngle = glm::radians(day * earth.orbitSpeed);
        earthModel = glm::rotate(earthModel, earthOrbitAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        earthModel = glm::translate(earthModel, glm::vec3(earth.orbitRadius, 0.0f, 0.0f));
        earthModel = glm::rotate(earthModel, glm::radians(earth.axisTilt), glm::vec3(1.0f, 0.0f, 0.0f));
        float earthSelfRotationAngle = glm::radians(day * earth.rotationSpeed * earth.rotationDirection);
        earthModel = glm::rotate(earthModel, earthSelfRotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        processInput(window);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(glm::radians(fov), (float)SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);

        drawSkySphere(shaderProgram, view, projection);
        drawCelestialBody(shaderProgram, view, projection, cameraPos, sun);
        for (const auto& celestialBody : celestialBodies) {
            drawCelestialBody(shaderProgram, view, projection, cameraPos, celestialBody, sunModel);
        }
        drawCelestialBody(shaderProgram, view, projection, cameraPos, moon, earthModel);
        day += 10.0f * deltaTime;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}


