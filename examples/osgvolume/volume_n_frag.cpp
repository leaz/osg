char volume_n_frag[] = "uniform sampler3D baseTexture;\n"
                       "uniform sampler3D normalMap;\n"
                       "uniform float sampleDensity;\n"
                       "uniform float transparency;\n"
                       "uniform float alphaCutOff;\n"
                       "\n"
                       "varying vec4 cameraPos;\n"
                       "varying vec4 vertexPos;\n"
                       "varying mat4 texgen;\n"
                       "\n"
                       "void main(void)\n"
                       "{ \n"
                       "\n"
                       "    vec3 t0 = (texgen * vertexPos).xyz;\n"
                       "    vec3 te = (texgen * cameraPos).xyz;\n"
                       "\n"
                       "    vec3 eyeDirection = normalize(te-t0);\n"
                       "\n"
                       "    if (te.x>=0.0 && te.x<=1.0 &&\n"
                       "        te.y>=0.0 && te.y<=1.0 &&\n"
                       "        te.z>=0.0 && te.z<=1.0)\n"
                       "    {\n"
                       "        // do nothing... te inside volume\n"
                       "    }\n"
                       "    else\n"
                       "    {\n"
                       "        if (te.x<0.0)\n"
                       "        {\n"
                       "            float r = -te.x / (t0.x-te.x);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "\n"
                       "        if (te.x>1.0)\n"
                       "        {\n"
                       "            float r = (1.0-te.x) / (t0.x-te.x);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "\n"
                       "        if (te.y<0.0)\n"
                       "        {\n"
                       "            float r = -te.y / (t0.y-te.y);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "\n"
                       "        if (te.y>1.0)\n"
                       "        {\n"
                       "            float r = (1.0-te.y) / (t0.y-te.y);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "\n"
                       "        if (te.z<0.0)\n"
                       "        {\n"
                       "            float r = -te.z / (t0.z-te.z);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "\n"
                       "        if (te.z>1.0)\n"
                       "        {\n"
                       "            float r = (1.0-te.z) / (t0.z-te.z);\n"
                       "            te = te + (t0-te)*r;\n"
                       "        }\n"
                       "    }\n"
                       "\n"
                       "    const float max_iteratrions = 2048.0;\n"
                       "    float num_iterations = ceil(length(te-t0)/sampleDensity);\n"
                       "    if (num_iterations<2.0) num_iterations = 2.0;\n"
                       "\n"
                       "    if (num_iterations>max_iteratrions) \n"
                       "    {\n"
                       "        num_iterations = max_iteratrions;\n"
                       "    }\n"
                       "\n"
                       "    vec3 deltaTexCoord=(te-t0)/float(num_iterations-1.0);\n"
                       "    vec3 texcoord = t0;\n"
                       "\n"
                       "    vec4 fragColor = vec4(0.0, 0.0, 0.0, 0.0); \n"
                       "    while(num_iterations>0.0)\n"
                       "    {\n"
                       "        vec4 normal = texture3D( normalMap, texcoord);\n"
                       "#if 1\n"
                       "        vec4 color = texture3D( baseTexture, texcoord);\n"
                       "\n"
                       "        normal.x = normal.x*2.0-1.0;\n"
                       "        normal.y = normal.y*2.0-1.0;\n"
                       "        normal.z = normal.z*2.0-1.0;\n"
                       "        \n"
                       "        float lightScale = 0.1 +  max(dot(normal.xyz, eyeDirection), 0.0);\n"
                       "        color.x *= lightScale;\n"
                       "        color.y *= lightScale;\n"
                       "        color.z *= lightScale;\n"
                       "\n"
                       "        float r = normal[3]*transparency;\n"
                       "#else\n"
                       "        vec4 color = texture3D( normalMap, texcoord);\n"
                       "        color.x = color.x*2.0 - 1.0;\n"
                       "        color.y = color.y*2.0 - 1.0;\n"
                       "        color.z = color.z*2.0 - 1.0;\n"
                       "        \n"
                       "        float lightScale = 0.1 +  max(dot(color.xyz, eyeDirection), 0.0);\n"
                       "        color.x = lightScale;\n"
                       "        color.y = lightScale;\n"
                       "        color.z = lightScale;\n"
                       "\n"
                       "        float r = color[3]*transparency;\n"
                       "#endif        \n"
                       "        if (r>alphaCutOff)\n"
                       "        {\n"
                       "            fragColor.xyz = fragColor.xyz*(1.0-r)+color.xyz*r;\n"
                       "            fragColor.w += r;\n"
                       "        }\n"
                       "        texcoord += deltaTexCoord; \n"
                       "\n"
                       "        --num_iterations;\n"
                       "    }\n"
                       "\n"
                       "    if (fragColor.w>1.0) fragColor.w = 1.0; \n"
                       "    if (fragColor.w==0.0) discard;\n"
                       "    gl_FragColor = fragColor;\n"
                       "}\n"
                       "\n";