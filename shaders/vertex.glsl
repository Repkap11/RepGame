
uniform vec3 cameraPosition;
uniform vec3 cameraUnit;
uniform vec3 lightPosition;

varying vec3 fragmentNormal;
varying vec3 cameraVector;
varying vec3 lightVector;
varying float cameraDistance;

void
main()
{
	vec4 cameraAdjust = gl_ModelViewProjectionMatrix * vec4(cameraPosition,1);
	//vec4 cameraAdjust = vec4(cameraPosition,1);
	//vec4(normalize(cameraUnit),1)
	// set the normal for the fragment shader and
	// the vector from the vertex to the camera
	fragmentNormal = gl_Normal;
	cameraVector = cameraPosition - gl_Vertex.xyz;// + vec3(0,-10,0);
	//cameraDistance = distance(cameraAdjust,gl_Vertex);
	//cameraDistance = -gl_Position.z;

	// set the vectors from the vertex to each light
	lightVector = lightPosition - gl_Vertex.xyz;

	// output the transformed vertex
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	//gl_Position = vec4(0,0,0,1);
}
