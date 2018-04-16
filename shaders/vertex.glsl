
uniform vec4 u_CameraPosition;
uniform vec4 u_CameraUnit;
uniform vec4 u_LightPosition;

varying vec3 fragmentNormal;
varying vec3 cameraVector;
varying vec3 lightVector;
varying float cameraDistance;

void
main()
{
	vec4 cameraAdjust = gl_ModelViewProjectionMatrix * u_CameraPosition;
	//vec4 cameraAdjust = vec4(cameraPosition,1);
	//vec4(normalize(u_CameraUnit),1)
	// set the normal for the fragment shader and
	// the vector from the vertex to the camera
	fragmentNormal = gl_Normal;
	cameraVector = u_CameraPosition.xyz - gl_Vertex.xyz;// + vec3(0,-10,0);
	//cameraDistance = distance(cameraAdjust,gl_Vertex);
	//cameraDistance = -gl_Position.z;

	// set the vectors from the vertex to each light
	lightVector = u_LightPosition.xyz - gl_Vertex.xyz;

	// output the transformed vertex
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	//gl_Position = vec4(0,0,0,1);
}
