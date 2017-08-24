#include <GL/glut.h>
#ifdef FREEGLUT
#include <GL/freeglut_ext.h>
#endif

#define WIDTH 640
#define HEIGHT 480

#include "cv.h"
#include "highgui.h"

const int MAX_CORNERS = 100;

CvPoint2D32f cornersA[MAX_CORNERS] = { 0 };
CvPoint2D32f cornersB[MAX_CORNERS] = { 0 };

char features_found[MAX_CORNERS];
float feature_errors[MAX_CORNERS];

int corner_count = MAX_CORNERS;

IplImage *prev_frame = 0;
IplImage *eig_image = 0;
IplImage *temp_image = 0;
IplImage *pyrA;
IplImage *pyrB;

enum {
	MODE_CALIBRATE,
	MODE_TRACK
};

int m_mode = MODE_CALIBRATE;

static GLuint texName[3];
GLubyte texture[256][256][4];

float m_zoom, m_rotx, m_roty, m_rotz, m_ofsx, m_ofsy;
int m_lastx = 0;
int m_lasty = 0;
unsigned char m_buttons[3] = { 0 };

int m_frame = 0;
int m_update = 0;

int fullscreen = 0;
int timerrate = 20;

const int m_gridw = 20;
const int m_gridh = 20;

float m_znear = 1.0f;
float m_zfar = 3000.0f;

int m_width = WIDTH;
int m_height = HEIGHT;

void clearcv()
{
	m_update = 1;
}

void togglefullscreen()
{
	if (fullscreen)
	{
		glutPositionWindow((glutGet(GLUT_SCREEN_WIDTH) - WIDTH) / 2, (glutGet(GLUT_SCREEN_HEIGHT) - HEIGHT) / 2);
		glutReshapeWindow(WIDTH, HEIGHT);
	}
	else
		glutFullScreenToggle();

	glutPostRedisplay();

	fullscreen = !fullscreen;

	clearcv();
}

void cleartransform()
{
	m_zoom = 30.0f;
	m_rotx = m_roty = m_rotz = m_ofsx = m_ofsy = 0.0f;

	clearcv();
}

void put_cell(int *buf, int w, int h, int x, int y, int cw, int ch, int color)
{
	for (int i = 0; i < ch; i++)
	{
		for (int j = 0; j < cw; j++)
		{
			int tx = (x + j) % w;
			int ty = (y + i) % h;

			buf[ty * w + tx] = color;
		}
	}
}

void gen_checker_texture(int *buf, int w, int h)
{
	int c = 8;

	int cw = 256 / c;
	int ch = 256 / c;

	for (int i = 0; i < h; i += cw * 2)
	{
		for (int j = 0; j < w; j += ch * 2)
		{
			int color = 0xffffff;
			put_cell(buf, w, h, i, j, cw, ch, color);
		}
	}

	for (int i = cw; i < h; i += cw * 2)
	{
		for (int j = ch; j < w; j += ch * 2)
		{
			int color = 0xffffff;
			put_cell(buf, w, h, i, j, cw, ch, color);
		}
	}

}

void init()
{
	cleartransform();
}

void drawString(int x, int y, const char *string, ...)
{
	char buffer[2048];

	va_list arg;
	va_start(arg, string);
	vsprintf(buffer, string, arg);
	va_end(arg);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	gluOrtho2D(0, m_width, m_height, 0);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glColor3f(1, 1, 1);

	glLogicOp(GL_XOR);
	glEnable(GL_COLOR_LOGIC_OP);
	glDisable(GL_LIGHTING);

	glRasterPos2i(x, y);
	int length = strlen(buffer);

	for (int i = 0; i < length; ++i)
	{
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, buffer[i]);
	}

	glLogicOp(GL_CLEAR);
	glDisable(GL_COLOR_LOGIC_OP);

	glEnable(GL_LIGHTING);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

void timer(int value)
{
	glutTimerFunc(timerrate, timer, 1);
	glutPostRedisplay();
}

void glColor(unsigned int color)
{
	const float k = 1.0f / 255.0f;
	float a = ((color >> 24) & 255) * k;
	float b = (color & 255) * k;
	float g = ((color >> 8) & 255) * k;
	float r = ((color >> 16) & 255) * k;
	r = r > 1 ? 1 : r;
	g = g > 1 ? 1 : g;
	b = b > 1 ? 1 : b;
	a = a == 0 ? 1 : a;
	glColor4f(r * a, g * a, b * a, a);
}

static void drawLine(float x1, float y1, float x2, float y2, int color)
{
	glColor(color);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	gluOrtho2D(0, m_width, m_height, 0);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_LIGHTING);

	glBegin(GL_LINES);
	glVertex2f(x1, y1);
	glVertex2f(x2, y2);
	glEnd();

	glEnable(GL_LIGHTING);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

static void drawBox(GLfloat size)
{

	static GLfloat n[6][3] = {
		{-1.0, 0.0, 0.0},
		{0.0, 1.0, 0.0},
		{1.0, 0.0, 0.0},
		{0.0, -1.0, 0.0},
		{0.0, 0.0, 1.0},
		{0.0, 0.0, -1.0}
	};

	static GLint faces[6][4] = {
		{0, 1, 2, 3},
		{3, 2, 6, 7},
		{7, 6, 5, 4},
		{4, 5, 1, 0},
		{5, 6, 2, 1},
		{7, 4, 0, 3}
	};
	GLfloat v[8][3];
	GLint i;

	v[0][0] = v[1][0] = v[2][0] = v[3][0] = -size / 2;
	v[4][0] = v[5][0] = v[6][0] = v[7][0] = size / 2;
	v[0][1] = v[1][1] = v[4][1] = v[5][1] = -size / 2;
	v[2][1] = v[3][1] = v[6][1] = v[7][1] = size / 2;
	v[0][2] = v[3][2] = v[4][2] = v[7][2] = -size / 2;
	v[1][2] = v[2][2] = v[5][2] = v[6][2] = size / 2;


	for (i = 5; i >= 0; i--)
	{
		glBegin(GL_QUADS);
		glNormal3fv(&n[i][0]);

		glTexCoord2f(0, 0);
		glVertex3fv(&v[faces[i][0]][0]);

		glTexCoord2f(0, 1);
		glVertex3fv(&v[faces[i][1]][0]);

		glTexCoord2f(1, 1);
		glVertex3fv(&v[faces[i][2]][0]);

		glTexCoord2f(1, 0);
		glVertex3fv(&v[faces[i][3]][0]);
    
		glEnd();
	}
}


void drawplane()
{
	glBindTexture(GL_TEXTURE_2D, texName[0]);

	glEnable(GL_TEXTURE_2D);

	drawBox(12);

	glDisable(GL_TEXTURE_2D);
}

void drawgrid()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);

	int gw = 20;
	int gh = 20;

	glColor3f(0.25f, 0.25f, 0.25f);

	glBegin(GL_LINES);

	for (int i = -gw / 2; i <= gw / 2; ++i)
	{
		glVertex3f(i, -gw / 2, 0);
		glVertex3f(i, gw / 2, 0);
	}

	for (int j = -gh / 2; j <= gh / 2; ++j)
	{
		glVertex3f(gh / 2, j, 0);
		glVertex3f(-gh / 2, j, 0);
	}

	glEnd();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);

}

void display()
{
	glViewport(0, 0, m_width, m_height);
	glMatrixMode(GL_PROJECTION);

	glLoadIdentity();

	//glOrtho(0, w, h, 0, -1, 1);

	gluPerspective(45, (float)m_width / m_height, m_znear, m_zfar);

	glTranslatef(m_ofsx, m_ofsy, -m_zoom);

	glRotatef(m_rotx, 1, 0, 0);
	glRotatef(m_roty, 0, 1, 0);
	glRotatef(m_rotz, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//drawgrid();
	drawplane();

	bool repaint = (m_buttons[0] == 0 && m_buttons[2] == 0);

	if (m_update && repaint)
	{
		int w = m_width;
		int h = m_height;

		IplImage *frame = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, 3);

		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, frame->imageData);
		cvFlip(frame, frame, 0);

		IplImage *imgA = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, 1);
		cvCvtColor(frame, imgA, CV_BGR2GRAY);

		switch (m_mode)
		{
			case MODE_CALIBRATE:
			{
				CvSize img_size = cvSize(w, h);
				CvSize board_size = cvSize(5, 5);
				cvFindChessboardCorners(imgA, board_size, cornersA, &corner_count, CV_CALIB_CB_ADAPTIVE_THRESH);
			}
				break;

			case MODE_TRACK:
				if (prev_frame)
				{
					IplImage *imgB = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, 1);
					cvCvtColor(prev_frame, imgB, CV_BGR2GRAY);

					double quality_level = 0.1;
					double min_distance = 5;
					int eig_block_size = 3;
					int use_harris = false;

					int win_size = 10;

					cvGoodFeaturesToTrack(imgA, eig_image, temp_image, cornersA, &corner_count, quality_level, min_distance, NULL, eig_block_size, use_harris);
					cvFindCornerSubPix(imgA, cornersA, corner_count, cvSize(win_size, win_size), cvSize(-1, -1), cvTermCriteria(CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, 0.03));

					cvCalcOpticalFlowPyrLK(imgA, imgB, pyrA, pyrB, cornersA, cornersB, corner_count, cvSize(win_size, win_size), 5, features_found, feature_errors,
							       cvTermCriteria(CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, .3), 0);

					cvReleaseImage(&imgB);
				}
				break;
		}

		if (prev_frame)
			m_update = 0;

		if (!prev_frame)
        	prev_frame = cvCreateImage(cvSize(w, h), IPL_DEPTH_8U, 3);

		cvCopy(frame, prev_frame);

		cvReleaseImage(&frame);
		cvReleaseImage(&imgA);

	}

	if (repaint)
	{
		switch (m_mode)
		{
			case MODE_CALIBRATE:
				for (int i = 0; i < corner_count; i++)
				{
					drawString(cornersA[i].x, cornersA[i].y, ".%d", i);
				}
				break;
			case MODE_TRACK:
				for (int i = 0; i < corner_count; i++)
				{
					int maxerr = 550;
					int found = features_found[i];
					int error = feature_errors[i];

					int color = error < maxerr / 3 ? 0x00ff00 : error < maxerr / 3 * 2 ? 0xffff00 : 0xff0000;

					if (found != 0 && error < maxerr)
					{
						drawLine(cornersA[i].x, cornersA[i].y, cornersB[i].x, cornersB[i].y, color);
					}
				}

				break;
		}
	}

	int x = 10;
	int y = 20;
	int dy = 16;

	drawString(x, y, "c - calibrate");
	y += dy;
	drawString(x, y, "t - track");
	y += dy;

	glutSwapBuffers();
}

void reshape(int w, int h)
{
	m_width = w;
	m_height = h;

	prev_frame = 0;

	eig_image = cvCreateImage(cvSize(w, h), IPL_DEPTH_32F, 1);
	temp_image = cvCreateImage(cvSize(w, h), IPL_DEPTH_32F, 1);

	CvSize pyr_sz = cvSize(w + 8, h / 3);

	pyrA = cvCreateImage(pyr_sz, IPL_DEPTH_32F, 1);
	pyrB = cvCreateImage(pyr_sz, IPL_DEPTH_32F, 1);

	clearcv();

	glutPostRedisplay();
}

void wheel(int wheel, int direction, int x, int y)
{
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )

	if (direction > 0)
	{
		m_zoom = max(0.9f * m_zoom, 0.01f);
	}
	else
	{
		m_zoom = min(1.1f * m_zoom, 1000.0f);
	}

	glutPostRedisplay();
}

void motion(int x, int y)
{
	float dx = (x - m_lastx);
	float dy = (y - m_lasty);

	m_lastx = x;
	m_lasty = y;

	if (m_buttons[0])
	{
		m_rotx += dy * 0.5f;
		m_rotz += dx * 0.5f;
	}
	else if (m_buttons[2])
	{
		m_ofsx += dx * 0.2f;
		m_ofsy += -dy * 0.2f;
	}

	glutPostRedisplay();
}

void keyboard(unsigned char key, int x, int y)
{
	if (key == 13 && glutGetModifiers() && GLUT_ACTIVE_ALT)
	{
		togglefullscreen();
		return;
	}

	switch (key)
	{
		case '+':
			m_zoom = max(0.99f * m_zoom, 1.0f);
			break;

		case '-':
			m_zoom = min(1.01f * m_zoom, 1000.0f);
			break;


		case 27:
		case 3:	//esc, ctrl+c
			exit(0);
			break;

		case 'f':
			togglefullscreen();
			break;

		case 13:
		case 'r':	//enter
			cleartransform();
			break;

		case 'c':
			m_mode = MODE_CALIBRATE;
			break;

		case 't':
			m_mode = MODE_TRACK;
			break;

	}
}

void special(int key, int x, int y)	// Create Special Function (required for arrow keys)
{
	switch (key)
	{
		case GLUT_KEY_F4:
			if (glutGetModifiers() && GLUT_ACTIVE_SHIFT)
				exit(0);
			break;
	}
}

void mouse(int b, int s, int x, int y)
{
	m_lastx = x;
	m_lasty = y;

	m_update = 1;

	switch (b)
	{
		case GLUT_LEFT_BUTTON:
			m_buttons[0] = ((GLUT_DOWN == s) ? 1 : 0);
			break;

		case GLUT_MIDDLE_BUTTON:
			m_buttons[1] = ((GLUT_DOWN == s) ? 1 : 0);
			break;

		case GLUT_RIGHT_BUTTON:
			m_buttons[2] = ((GLUT_DOWN == s) ? 1 : 0);
			break;
	}

	glutPostRedisplay();
}


int main(int argc, char **argv)
{

	glutInit(&argc, argv);

	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);

	glutInitWindowPosition((glutGet(GLUT_SCREEN_WIDTH) - WIDTH) / 2, (glutGet(GLUT_SCREEN_HEIGHT) - HEIGHT) / 2);
	glutInitWindowSize(WIDTH, HEIGHT);

	glutCreateWindow(argv[0]);

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutSpecialFunc(special);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	glEnable(GL_DEPTH_TEST);

	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);

#ifdef FREEGLUT
	glutMouseWheelFunc(wheel);
#endif

	gen_checker_texture((int *)texture, 256, 256);

	glBindTexture(GL_TEXTURE_2D, texName[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, &texture[0][0][0]);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	init();
	glutTimerFunc(timerrate, timer, 1);
	glutMainLoop();

	return 0;
}
